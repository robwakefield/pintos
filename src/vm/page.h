#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdint.h>
#include <hash.h>
#include "devices/swap.h"
#include "threads/palloc.h"
#include "filesys/file.h"
#include "vm/frame.h"
#include "threads/synch.h"


enum page_status
  {
    ALL_ZERO,       /* Zeroed page (new page). */
    FRAME,          /* Frame allocated to page. */
    SWAPPED,        /* Page is swapped (in swap slot). */
    FILE      
  };

/* Virtual page. */
struct page 
  {
    void *addr;                 /* User virtual address. */
    bool writable;             /* Read-only page? */
    struct thread *owner;      /* Owning thread. */

    enum page_status status; 

    struct hash_elem hash_elem; 

    /* Set only in owning process context with frame->frame_lock held.
       Cleared only with scan_lock and frame->frame_lock held. */
    struct frame_entry *frame;  /* Page frame. */

    struct file *file;          /* File. */
    off_t offset;               /* Offset in file. */
    off_t file_bytes;           /* Bytes to read/write, 1...PGSIZE. */
  };

unsigned page_hash (const struct hash_elem *e, void *aux UNUSED);
bool page_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);

struct page *page_by_addr (const void *addr);
bool load_page (struct page *p);

void page_table_destroy (void);

struct page *page_alloc (void *, bool writable);
void page_dealloc (void *vaddr);

bool page_in (void *fault_addr);
bool page_out (struct page *);
bool page_accessed_recently (struct page *);

#endif /* vm/page.h */