#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdint.h>
#include <hash.h>
#include "devices/swap.h"
#include "threads/palloc.h"
#include "filesys/file.h"
#include "vm/frame.h"
#include "threads/synch.h"
#include <list.h>


enum page_status
  {
    ALL_ZERO,       /* Zeroed page (new page). */
    IN_FRAME,          /* Frame allocated to page. */
    SWAPPED,        /* Page is swapped (in swap slot). */
    FILE,
    MMAPPED     
  };

/* Virtual page. */
struct page 
  {
    void *addr;                 /* User virtual address. */
    bool writable;             /* Read-only page? */
    bool dirty;
    struct thread *owner;      /* Owning thread. */

    enum page_status status;

    struct hash_elem hash_elem; 

    /* Set only in owning process context with frame->frame_lock held.
       Cleared only with scan_lock and frame->frame_lock held. */
    void *kpage;                /* */

    struct file *file;          /* File. */
    off_t offset;               /* Offset in file. */
    uint32_t read_bytes;           /* Bytes to read/write, 1...PGSIZE. */
    uint32_t zero_bytes;         

    size_t swap_slot;  
    struct list_elem list_elem;
  };

unsigned page_hash (const struct hash_elem *e, void *aux UNUSED);
bool page_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);

void page_table_destroy (void);
void page_dealloc (struct hash *pt, struct page *p);

struct page * page_lookup (struct hash *pt, const void *addr);

struct page * page_alloc_zeroed (struct hash *pt, void *vaddr);
bool page_set_dirty (struct hash *pt, void *vaddr, bool dirty);

bool page_alloc_with_file (struct hash *pt, void *upage, struct file *file, off_t offset, 
                      uint32_t read_bytes, uint32_t zero_bytes, bool writable);
bool page_alloc_mmap (struct hash *pt, void *upage, struct file *file, off_t offset, 
                      uint32_t read_bytes, uint32_t zero_bytes, bool writable);
bool page_install_frame (struct hash *pt, void *upage, void *kpage);

bool load_file (void *kpage, struct page *p);

bool
page_munmap(struct hash *pt, uint32_t *pagedir, void *upage, struct file *f, off_t offset, size_t bytes);

#endif /* vm/page.h */
