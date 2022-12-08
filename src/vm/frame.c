#include "frame.h"
#include "page.h"
#include "devices/swap.h"
#include "userprog/pagedir.h"
#include <stdio.h>

static struct hash *frame_table;
static struct list frame_list;
static struct lock frame_table_lock;
static struct lock frame_list_lock;

static struct list_elem *clock_ptr;
static struct list_elem *resetting_ptr;

/* Initialization code for frame table. */
void
frame_table_init (void) {
  lock_init (&frame_table_lock);
  lock_init (&frame_list_lock);
  frame_table = malloc (sizeof (struct hash));
  hash_init (frame_table, frame_hash, frame_less_func, NULL);
  list_init (&frame_list);

  clock_ptr = NULL;
  resetting_ptr = NULL;
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
create_entry (void *frame) {
  struct frame_entry *f = malloc (sizeof (struct frame_entry));

  f->owner = thread_current ();
  f->frame_address = frame;
  
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

/* Adds a given frame table entry to the frame table. */
void
add_frame (void *frame) {
  lock_acquire (&frame_table_lock);

  struct frame_entry *entry = create_entry (frame); 
  ASSERT (hash_insert (frame_table, &entry->hash_elem) == NULL);
  
  lock_release (&frame_table_lock);

}

/* Frees all resources used by frame table entry
and removes entry from the frame table. */
void
frame_free (void *frame) {
  lock_acquire (&frame_table_lock);

  struct frame_entry *temp_entry = search_elem (frame);
  struct hash_elem *removed = hash_delete (frame_table, &temp_entry->hash_elem);
  free (temp_entry); 

  // TODO: check this
  if (removed != NULL) {
    /* Deallocate the frame table entry. */
    struct frame_entry *f = hash_entry (removed, struct frame_entry, hash_elem);
    free (f);
    palloc_free_page (pg_round_down (frame));
  }
  lock_release (&frame_table_lock);
}

/* Allocates a frame for the given page. */
void *
frame_alloc (enum palloc_flags flags, void *upage) {
  bool evicted = false;

  lock_acquire(&frame_table_lock);

  void *f_page = palloc_get_page (PAL_USER | flags);

  if(f_page == NULL) {
    /* Page allocation faied -> choose page to evict. */
    struct frame_entry *frame;

    while (!evicted) {
      frame = list_entry (clock_ptr, struct frame_entry, list_elem);

      if (frame->pinned) {
        /* Frame is pinned, cannot evict. Move on to next frame. */
        clock_hand_move (clock_ptr);
        continue;
      } 
  
      if (pagedir_is_accessed (frame->owner->pagedir, frame->upage)) {
        /* Referenced bit set -> give second chance and move clock pointer. */
        pagedir_set_accessed (frame->owner->pagedir, frame->upage, false);
        pagedir_set_accessed (frame->owner->pagedir, frame->frame_address, false);
      
      } else {
        /* Referenced bit not set -> evict page. */
        struct page *p = page_lookup (frame->owner->page_table, frame->upage);
        ASSERT (p != NULL);

        /* Swap out page. */
        p->swap_slot = swap_out (p->addr);
        p->kpage = NULL;
        p->status = SWAPPED;
        pagedir_clear_page (frame->owner->pagedir, p->addr);

        /* Remove frame. */
        frame_free (frame);
        list_remove (clock_ptr);

        evicted = true;
      }

      clock_hand_move (clock_ptr);
      // reset_hand_move (resetting_ptr);
    }

    /* Allocate after page eviction -> should succeed in this chance. */
    f_page = palloc_get_page (PAL_USER | flags);
    ASSERT (fpage != NULL);;
  }

  struct frame_entry *new_frame = malloc (sizeof (struct frame_entry));
  ASSERT (frame != NULL);

  lock_acquire (&frame_list_lock);
  list_insert (clock_ptr, &new_frame->list_elem);
  lock_release (&frame_list_lock);

  // move clock hand?
  
  struct thread *t = thread_current ();

  /* Sets new page information (both for page-in and page replacement). */
  new_frame->owner = t;
  new_frame->frame_address = f_page;
  new_frame->upage = upage;
  
  /* Pin frame until page loaded -> synchronises while loading page data into the frame. */
  new_frame->pinned = true;

  if (upage != NULL) {
    pagedir_set_accessed (t->pagedir, upage, true);
    pagedir_set_accessed (t->pagedir, f_page, true);
    /* TODO: what user virtual addresses to allocate e.g. in setup stack? */
    if (pagedir_is_dirty (t->pagedir, upage) || pagedir_is_dirty (t->pagedir, f_page)) {
      pagedir_set_dirty (t->pagedir, upage, true);
      pagedir_set_dirty (t->pagedir, f_page, true);
    }
  }

  lock_release (&frame_table_lock);

  return f_page;
}

void
clock_hand_move (struct list_elem *ptr)
{
  ASSERT (!list_empty(&frame_list));

  if (ptr == NULL || ptr == list_end (&frame_list)) {
    ptr = list_begin (&frame_list);
  } else {
    ptr = list_next (ptr);
  }
}

void
reset_hand_move (void)
{
  ASSERT (!list_empty(&frame_list));

  /* Set reference bit of page pointed to by the second clock hand to 0. */
  struct frame_entry *frame = list_entry (clock_ptr, struct frame_entry, list_elem);
  pagedir_set_accessed (frame->owner->pagedir, frame->upage, false);

  clock_hand_move (resetting_ptr);
}

void
frame_set_pinned (void *kpage, bool pinned)
{
  lock_acquire (&frame_table_lock);

  // hash lookup : a temporary entry
  struct frame_entry f_tmp;
  f_tmp.kpage = kpage;
  struct hash_elem *h = hash_find (&frame_table, &(f_tmp.hash_elem));
  if (h == NULL) {
    printf ("The frame to be pinned/unpinned does not exist\n");
  }

  struct frame_entry *f;
  f = hash_entry(h, struct frame_entry, hash_elem);
  f->pinned = pinned;

  lock_release (&frame_table_lock);
}