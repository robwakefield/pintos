#include "vm/page.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "devices/swap.h"
#include "userprog/syscall.h"

static void page_destroy (struct hash_elem *e, void *aux UNUSED);

unsigned
page_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct page *p = hash_entry (e, struct page, hash_elem);
  return hash_int ((int) p->addr);
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

  /* TODO: Lock frame or page? */

  if (p->status == IN_FRAME) {
    ASSERT (p->kpage != NULL);
    frame_free (p->kpage, false);
  } else if (p->status == SWAPPED){
    //TODO: swap_drop
    ASSERT ((int) p->swap_slot != -1);
    swap_drop(p->swap_slot);
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

void
page_dealloc (struct hash *pt, struct page *p)
{

  if (hash_delete (pt, &p->hash_elem) != NULL)
  {
    if (p->kpage != NULL) {
      frame_free (p->kpage, true);
    }
    if (pagedir_get_page (thread_current ()->pagedir, p->addr) != NULL) {
      pagedir_clear_page (thread_current ()->pagedir, p->addr);
    }  
  }

  free (p);
}

struct page *
page_lookup (struct hash *pt, const void *addr)
{
  struct page *temp = malloc (sizeof (struct page));
  struct hash_elem *e;

  temp->addr = pg_round_down (addr);
  e = hash_find (pt, &temp->hash_elem);

  if (e == NULL) {
    //printf ("ERROR: hash_find returned null -> page is not in page table\n");
  } else {
    //printf("(page)hash found in page table\n");
  }

  free (temp);
  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

struct page *
page_alloc_zeroed (struct hash *pt, void *vaddr) {
  struct page *p = malloc (sizeof (struct page));

  p->addr = vaddr;
  p->kpage = NULL;
  p->status = ALL_ZERO;
  p->dirty = false;
  p->kpage = NULL;
  p->read_bytes = 0;
  p->zero_bytes = PGSIZE;
  p->writable = true;
  p->owner = thread_current ();

  ASSERT (hash_insert (pt, &p->hash_elem) == NULL);

  return p;
}

bool
page_set_dirty (struct hash *pt, void *vaddr, bool dirty) {
  struct page *p = page_lookup (pt, vaddr);
  if (p == NULL) {
    return false;
  }

  p->dirty = p->dirty || dirty;
  return true;
}

/* Creates a supplemental page table for a file page
   Takes the user virtual address, file, file offset, whether it is writable,
   the number of bytes to read and its page type 
   Returns false if memory allocation fails or duplicate MMAP created */
bool 
page_alloc_with_file (struct hash *pt, void *upage, struct file *file, off_t offset, 
                      uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  /* Check if virtual page already allocated */
  struct page *p = page_lookup(pt, upage);
  
  if (p != NULL) {
    /* Update metadata if load_segment is loading same page twice */
    ////printf("loading same page twice\n");
    size_t new_read_bytes = p->read_bytes > read_bytes ? p->read_bytes : read_bytes;

    p->read_bytes = new_read_bytes;
    p->zero_bytes = PGSIZE - new_read_bytes;
    p->offset = offset;
    p->writable = p->writable || writable;
    p->owner = thread_current ();

    return true;
  }

  /* Creates new table entry with given values if one cannot be found */
  p = (struct page *) malloc (sizeof (struct page));
  if (p == NULL) {
    /* malloc failed */
    return false;
  }

  p->file = file;
  p->offset = offset;
  p->read_bytes = read_bytes;
  p->zero_bytes = zero_bytes;
  p->kpage = NULL;
  p->addr = upage;
  p->owner = thread_current();
  p->dirty = false;
  p->writable = writable;
  p->status = FILE;
    
  if (hash_insert (pt, &p->hash_elem) != NULL) {
    printf ("inserting already existing page in alloc_file\n");
    free (p);
  }

  return true;
}

bool 
page_alloc_mmap (struct hash *pt, void *upage, struct file *file, off_t offset, 
                      uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  /* Check if virtual page already allocated */
  struct page *p = page_lookup(pt, upage);
  
  if (p != NULL) {
    return false;
  }

  /* Creates new table entry with given values if one cannot be found */
  p = (struct page *) malloc (sizeof (struct page));
  ASSERT (p != NULL);

  p->file = file;
  p->offset = offset;
  p->read_bytes = read_bytes;
  p->zero_bytes = zero_bytes;
  p->kpage = NULL;
  p->addr = upage;
  p->owner = thread_current();
  p->dirty = false;
  p->writable = writable;
  p->status = MMAPPED;
    
  ASSERT (hash_insert (pt, &p->hash_elem) == NULL);

  return true;
}

bool
page_install_frame (struct hash *pt, void *upage, void *kpage)
{

  struct page *p = page_lookup (pt, upage);
  if (p == NULL) {
    printf ("installing frame for non-existing page\n");
    return false;
  }

  p->addr = upage;
  p->kpage = kpage;
  p->status = IN_FRAME;
  p->dirty = false;
  p->owner = thread_current ();

  return true;
}

/* After allocate physical memory, load file to physical page from disk. */
bool
load_file (void *kpage, struct page *p)
{
  lock_acquire (&filesys_lock);
  file_seek (p->file, p->offset);

  /* Load data into the page. */
  if (file_read (p->file, kpage, p->read_bytes) != (int) p->read_bytes) {
    printf ("load_file did not read enough bytes\n");
    lock_release (&filesys_lock);
    return false;
  }
  
  ASSERT (p->read_bytes + p->zero_bytes == PGSIZE);
  memset (kpage + p->read_bytes, 0, p->zero_bytes);

  lock_release (&filesys_lock);
  return true;
}

bool
page_munmap(struct hash *pt, uint32_t *pagedir,
    void *upage, struct file *f, off_t offset, size_t bytes)
{
  struct page *p = page_lookup (pt, upage);
  ASSERT (p != NULL);

  // Pin the associated frame if loaded
  // otherwise, a page fault could occur while swapping in (reading the swap disk)
  if (p->status == IN_FRAME) {
    ASSERT (p->kpage != NULL);
    frame_set_pinned (p->kpage, true);
  }


  // see also, vm_load_page()
  switch (p->status)
  {
  case IN_FRAME:
    ASSERT (p->kpage != NULL);

    // Dirty frame handling (write into file)
    // Check if the upage or mapped frame is dirty. If so, write to file.
    bool is_dirty = p->dirty;
    is_dirty = is_dirty || pagedir_is_dirty(pagedir, p->addr);
    is_dirty = is_dirty || pagedir_is_dirty(pagedir, p->kpage);
    if (is_dirty) {
      file_write_at (f, p->addr, bytes, offset);
    }

    // clear the page mapping, and release the frame
    frame_free (p->kpage, true);
    pagedir_clear_page (pagedir, p->addr);
    break;

  case SWAPPED:
    {
      bool is_dirty = p->dirty;
      is_dirty = is_dirty || pagedir_is_dirty (pagedir, p->addr);
      if (is_dirty) {
        // load from swap, and write back to file
        void *tmp_page = palloc_get_page (0); // in the kernel
        swap_in (tmp_page, p->swap_slot);
        file_write_at (f, tmp_page, PGSIZE, offset);
        palloc_free_page (tmp_page);
      }
      else {
        // just throw away the swap.
        swap_drop (p->swap_slot);
      }
    }
    break;

  case FILE:
    // do nothing.
    break;

  default:
    // Impossible, such as ALL_ZERO
    ASSERT (false);
  }

  // the supplemental page table entry is also removed.
  // so that the unmapped memory is unreachable. Later access will fault.
  page_dealloc (pt, p);
  return true;
}
