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
page_table_destroy (void)  {
  struct hash *pt = thread_current()->page_table;
  if (pt != NULL) {
    hash_destroy(pt, page_destroy);
  }
}