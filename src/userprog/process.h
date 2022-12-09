#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "vm/page.h"

#define MAX_STACK_SIZE 8000000

typedef int mapid_t;

struct mmap {
    mapid_t id;
    void *page;
    struct file *file;
    size_t size;
    struct list_elem map_elem;
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
bool grow_stack (void *vaddr);
bool load_file_page (struct page *p, void *kpage);
bool load_page(struct hash *pt, uint32_t *pagedir, struct page *p);
bool file_share_page (struct page *p);

#endif /* userprog/process.h */
