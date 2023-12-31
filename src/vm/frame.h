#ifndef FRAME_H
#define	FRAME_H

#include <debug.h>
#include <hash.h>
#include <list.h>
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "vm/page.h"

struct frame_entry
{
    void *frame_address;
    void *upage;

    struct list pages;
    struct lock pages_lock;

    bool pinned;
    struct thread *owner;
    struct hash_elem hash_elem;
    struct list_elem list_elem;
};

void frame_table_init (void);

unsigned frame_hash (const struct hash_elem *h, void *aux UNUSED);
bool frame_less_func (const struct hash_elem *e1, const struct hash_elem *e2, void *aux UNUSED);

void *frame_alloc (enum palloc_flags flags, void *upage);
void frame_free (void *kpage, bool free_page);

struct frame_entry *create_entry (void);
struct frame_entry *search_elem (void *address);
void remove_frame (void *frame);

bool eviction (enum palloc_flags flags);
void clock_hand_move (void);
void reset_hand_move (void);

void frame_set_pinned (void *kpage, bool pinned);

struct frame_entry *frame_find (void *kpage);
void add_to_pages (void *kpage, struct page *p);

#endif /* vm/frame.h */
