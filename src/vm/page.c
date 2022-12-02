#include "vm/page.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "devices/swap.h"

unsigned
page_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct page *p = hash_entry (e, struct page, hash_elem);
  return hash_bytes (p->addr, sizeof (p->addr));
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *fst, const struct hash_elem *snd,
           void *aux UNUSED)
{
  const struct page *a = hash_entry (fst, struct page, hash_elem);
  const struct page *b = hash_entry (snd, struct page, hash_elem);

  return a->addr < b->addr;
}

/* Adds a mapping for user virtual address VADDR to the page hash
   table.  Fails if VADDR is already ped or if memory
   allocation fails. */
struct page *
page_alloc (void *vaddr, bool writable)
{
  struct thread *t = thread_current ();
  struct page *p = malloc (sizeof *p);

  if (p != NULL) 
    {
      p->addr = pg_round_down (vaddr);

      p->writable = writable;
      p->frame = NULL;

      p->file = NULL;
      p->offset = 0;
      p->file_bytes = 0;

      p->owner = thread_current ();

      if (hash_insert (t->page_table, &p->hash_elem) != NULL) 
        {
          /* Already mapped. */
          free (p);
          p = NULL;
        }
    }
  return p;
}

/* Evicts the page containing address VADDR
   and removes it from the page table. */
// void
// page_dealloc (void *vaddr) 
// {
//   struct page *p = (struct page *) page_by_addr (vaddr);
//   ASSERT (p != NULL);

//   /* TODO: Lock frame of page or page itself?*/
//   //frame_lock (p);

//   if (p->frame) {
//       struct frame_entry *f = p->frame;
//       if (p->file) {
//         // page out
//       }
//       frame_free (f);
//   }

//   hash_delete (thread_current ()->page_table, &p->hash_elem);
//   free (p);
// }

/* Destroys a page from the process's page table. */
static void
page_destroy (struct hash_elem *e, void *aux UNUSED)
{
  struct page *p = hash_entry (e, struct page, hash_elem);
  //frame_lock (p);
  /* Lock frame or page? */

  if (p->frame) {
    frame_free (p->frame);
  }

  free (p);
}

/* Destroys the current process's page table. */
void
page_table_destroy (void) 
{
  struct hash *pt = thread_current ()->page_table;

  if (pt != NULL) {
    hash_destroy (pt, page_destroy);
  }
}

/* Returns the page containing the given virtual address,
   or a null pointer if there's no such page.*/
struct page *
page_by_addr (const void *addr) 
{
  if (addr < PHYS_BASE) {
    struct page p;
    struct hash_elem *e;

    /* Find existing page. */
    p.addr = (void *) pg_round_down (addr);
    e = hash_find (thread_current ()->page_table, &p.hash_elem);
    if (e != NULL) {
        return hash_entry (e, struct page, hash_elem);
    }
  }

  return NULL;
}

/* Returns true if page has been accessed recently,
   false otherwise.
   P must have a frame locked into memory. */
bool
page_accessed_recently (struct page *p) 
{
  bool was_accessed;

  ASSERT (p->frame != NULL);
  /* check if lock held by current thread? */

  was_accessed = pagedir_is_accessed (p->owner->pagedir, p->addr);
  if (was_accessed) {
    pagedir_set_accessed (p->owner->pagedir, p->addr, false);
  }

  return was_accessed;
}

bool
page_in (void *fault_addr) 
{
  struct page *p;
  bool success;

  if (thread_current ()->page_table == NULL){
    return false;
  }

  p = page_by_addr (fault_addr);
  if (p == NULL) {
    return false; 
  }

  // TODO: revise
  //frame_lock (p);

  if (p->frame == NULL) {
      if (!(load_page (p))) {
        return false;
      }
  }

  //ASSERT (lock_held_by_current_thread (&p->frame->lock));
    
  /* Install frame into page table. */
  success = pagedir_set_page (thread_current ()->pagedir, p->addr,
                              p->frame->frame_address, p->writable);

  /* Release frame. */
  //frame_unlock (p->frame);

  return success;
}

/* Loads page in. Returns true if successful, false on failure. */
bool
load_page (struct page *p)
{
  /* Get a frame for the page. */
  p->frame = frame_alloc (PAL_USER);
  if (p->frame == NULL){
    return false;
  }

  /* Copy data into the frame. */
  if (p->file != NULL) 
    {
      /* Get data from file. */
      size_t page_read_bytes = file_read_at (p->file, p->frame->frame_address,
                                        p->file_bytes, p->offset);
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
      memset (p->frame->frame_address+ page_read_bytes, 0, page_zero_bytes);

      if (page_read_bytes != p->file_bytes)
        printf ("bytes read %zu != bytes requested %zu\n",
                page_read_bytes, p->file_bytes);
    } else {
      /* Page is ALL ZERO. */
      memset (p->frame->frame_address, 0, PGSIZE);
    }

  return true;
}
