#ifndef FRAME_H
#define	FRAME_H

#include <debug.h>
#include <hash.h>
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/thread.h"

#define BUCKET_COUNT 16

struct frame_entry
{
    void *frame_address;
    struct thread *owner;
    struct hash_elem elem;
};

void frame_table_init (void);

unsigned frame_hash (const struct hash_elem *h, void *aux UNUSED);
bool frame_less_func (const struct hash_elem *e1, const struct hash_elem *e2, void *aux UNUSED);

void *frame_alloc (enum palloc_flags flags);
void frame_free (void *frame);

struct frame_entry *create_entry (void *frame);
struct hash_entry *search_elem (void *address);
void add_frame (void *frame);
void remove_frame (void *frame);

#endif
