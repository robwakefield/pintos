#include "frame.h"
#include "page.h"
#include "devices/swap.h"
#include "userprog/pagedir.h"
#include "threads/loader.h"
#include <stdio.h>

static struct hash *frame_table;
static struct list frame_list;
static struct lock frame_table_lock;
static struct lock frame_list_lock;

static struct list_elem *clock_ptr;
static struct list_elem *reset_ptr;

static bool reset_set = false;
static int no_of_clock_hand_moves = 0;

/* Initialization code for frame table. */
void
frame_table_init (void) {
  lock_init (&frame_table_lock);
  lock_init (&frame_list_lock);
  frame_table = malloc (sizeof (struct hash));
  hash_init (frame_table, frame_hash, frame_less_func, NULL);
  list_init (&frame_list);

  clock_ptr = NULL;
  reset_ptr = NULL;
}

/* Hash function for frame table. */
unsigned
frame_hash (const struct hash_elem *e, void *aux UNUSED) {
  const struct frame_entry *entry = hash_entry (e, struct frame_entry, hash_elem);
  return hash_bytes (&entry->frame_address, sizeof (entry->frame_address));
}

/* Comparing function for frame table. */
bool
frame_less_func (const struct hash_elem *e1, const struct hash_elem *e2, void *aux UNUSED) {
  const struct frame_entry *a = hash_entry (e1, struct frame_entry, hash_elem);
  const struct frame_entry *b = hash_entry (e2, struct frame_entry, hash_elem);
  return a->frame_address < b->frame_address;
}

/* Creates a new frame table entry, which stores the address of the frame, 
the process that owns the frame. */
struct frame_entry *
create_entry (void) {
  struct frame_entry *f = malloc (sizeof (struct frame_entry));
  list_init(&f->pages);
  lock_init(&f->pages_lock);

  f->owner = thread_current ();
  
  return f;
}

/* Creates a "fake" temporary hash element on the stack,
containing just enough information for hash_find to find the element we are looking for. */
struct frame_entry *
search_elem (void *address) {
  struct frame_entry *temp_entry;
  temp_entry = malloc (sizeof (struct frame_entry));
  temp_entry->frame_address = address;
  return temp_entry;
}

/* Frees all resources used by frame table entry
and removes entry from the frame table. */
void
frame_free (void *kpage, bool free_page) {
  ASSERT (is_kernel_vaddr (kpage));
  ASSERT (pg_ofs (kpage) == 0);
  
  lock_acquire (&frame_table_lock);

  struct frame_entry *frame = frame_find (kpage);
  ASSERT (frame != NULL);

  struct page *p;
  struct list_elem *e;
  bool referenced = false;

  if (!list_empty (&frame->pages)) {

    lock_acquire (&frame->pages_lock);

    for (e = list_begin (&frame->pages); e != list_end (&frame->pages);) {
      p = list_entry (e, struct page, list_elem);
      ASSERT (p != NULL);

      if (p->owner == thread_current ()) {
        e = list_remove (&p->list_elem);
        //TODO: free the page table entry?
        if (p->kpage == kpage) {
          page_dealloc(thread_current ()->page_table, p);
        }
      } else {
        e = list_next (e);
      }
    }
    referenced = !(list_empty (&frame->pages));

    lock_release (&frame->pages_lock);
  }


  if (!referenced) {
    /* Deallocate the frame table entry. */
    hash_delete (frame_table, &frame->hash_elem);
    /* Remove from list for clock algorithm. */
    list_remove (&frame->list_elem);

    if (free_page) {
       palloc_free_page (frame->frame_address);
    }
    free (frame);
  } 
  lock_release (&frame_table_lock);
}

/* Allocates a frame for the given page. */
void *
frame_alloc (enum palloc_flags flags, void *upage) {
  bool evict_success = false;

  void *f_page = palloc_get_page (PAL_USER | flags);

  if(f_page == NULL) {
    evict_success = eviction (flags);

    // /* Allocate after page eviction -> should succeed in this chance. */
    // f_page = palloc_get_page (PAL_USER | flags);
    // ASSERT (f_page != NULL);
  }

  lock_acquire(&frame_table_lock);

  struct frame_entry *new_frame = create_entry ();
  ASSERT (new_frame != NULL);

  lock_acquire (&frame_list_lock);
  if (evict_success) {
    /* If frame was evicted, insert new frame at clock pointer. */
    list_insert (clock_ptr, &new_frame->list_elem);
  } else {
    /* If no eviction was needed, insert new frame at the end of the frame list, move clock hand. */
    list_push_back (&frame_list, &new_frame->list_elem);
  }
  lock_release (&frame_list_lock);

  /* Sets new page information (both for page-in and page replacement). */
  new_frame->frame_address = f_page;
  new_frame->upage = upage;

  struct thread *t = thread_current ();
  if (upage != NULL) {
    pagedir_set_accessed (t->pagedir, upage, true);
    pagedir_set_accessed (t->pagedir, f_page, true);
    /* TODO: what user virtual addresses to allocate e.g. in setup stack? */
    if (pagedir_is_dirty (t->pagedir, upage) || pagedir_is_dirty (t->pagedir, f_page)) {
      pagedir_set_dirty (t->pagedir, upage, true);
      pagedir_set_dirty (t->pagedir, f_page, true);
    }
  }

  ASSERT (hash_insert (frame_table, &new_frame->hash_elem) == NULL);
  new_frame->pinned = false;

  lock_release (&frame_table_lock);

  return f_page;
}

bool
eviction (enum palloc_flags flags) {
  /* Page allocation failed -> choose page to evict. */
  bool evicted = false;
  struct frame_entry *frame;

  lock_acquire (&frame_table_lock);

  while (!evicted) {
    ASSERT (clock_ptr != NULL);
    frame = list_entry (clock_ptr, struct frame_entry, list_elem);

    if (frame->pinned) {
      /* Frame is pinned, cannot evict. Move on to next frame. */
      clock_hand_move ();
      continue;
    }
  
    if (pagedir_is_accessed (frame->owner->pagedir, frame->upage)) {
      /* Referenced bit set -> give second chance and move clock pointer. */
      pagedir_set_accessed (frame->owner->pagedir, frame->upage, false);
      pagedir_set_accessed (frame->owner->pagedir, frame->frame_address, false);
      clock_hand_move ();
      
    } else {
      /* Referenced bit not set -> evict page. */
      lock_acquire (&frame->pages_lock);
      struct list_elem *e;

      for (e = list_begin (&frame->pages); e != list_end (&frame->pages); e = list_next (e)) {
        struct page *p = list_entry (e, struct page, list_elem);
        ASSERT (p != NULL);

        /* Swap out page. */
        p->swap_slot = swap_out (p->kpage);
        p->kpage = NULL;
        p->status = SWAPPED;
        pagedir_clear_page (p->owner->pagedir, p->addr);
      }
      lock_release (&frame->pages_lock);

      /* Remove frame. */
      clock_hand_move ();
      lock_release (&frame_table_lock);
      frame_free (frame->frame_address, true);

      /* Allocate after page eviction -> should succeed in this chance. */
      void *f_page = palloc_get_page (PAL_USER | flags);
      evicted = (f_page != NULL);
    }
  }
  return evicted;
}

void
clock_hand_move (void)
{
  ASSERT (!list_empty(&frame_list));

  if (clock_ptr == NULL || clock_ptr == list_end (&frame_list)) {
    clock_ptr = list_begin (&frame_list);
  } else {
    clock_ptr = list_next (clock_ptr);
  }

  if (!reset_set) {
    no_of_clock_hand_moves += 1;
    
    if (no_of_clock_hand_moves > (int) (init_ram_pages/2)) {
      reset_ptr = list_begin (&frame_list);
      reset_set = true;
    }
  } else {
    reset_hand_move ();
  }
}

void
reset_hand_move (void)
{
  ASSERT (!list_empty(&frame_list));

  /* Set reference bit of page pointed to by the second clock hand to 0. */
  struct frame_entry *frame = list_entry (reset_ptr, struct frame_entry, list_elem);
  pagedir_set_accessed (frame->owner->pagedir, frame->upage, false);
  pagedir_set_accessed (frame->owner->pagedir, frame->frame_address, false);

  if (reset_ptr == NULL || reset_ptr == list_end (&frame_list)) {
    reset_ptr = list_begin (&frame_list);
  } else {
    reset_ptr = list_next (reset_ptr);
  }
}

void
frame_set_pinned (void *kpage, bool pinned)
{
  lock_acquire (&frame_table_lock);

  // hash lookup : a temporary entry
  struct frame_entry *temp = search_elem (kpage);
  struct hash_elem *h = hash_find (frame_table, &temp->hash_elem);
  free (temp);

  if (h == NULL) {
    printf ("The frame to be pinned/unpinned does not exist\n");
  }

  struct frame_entry *f;
  f = hash_entry(h, struct frame_entry, hash_elem);

  f->pinned = pinned;

  lock_release (&frame_table_lock);
}

struct frame_entry *
frame_find (void *kpage) {
  struct frame_entry *temp_entry = search_elem (kpage);
  struct hash_elem *e = hash_find (frame_table, &temp_entry->hash_elem);
  free (temp_entry); 

  return e != NULL ? hash_entry (e, struct frame_entry, hash_elem) : NULL;
}

void
add_to_pages (void *kpage, struct page *p) {
  struct frame_entry *f = frame_find (kpage);

  if (p != NULL && f != NULL) {
    list_push_back (&f->pages, &p->list_elem);
  }
}
