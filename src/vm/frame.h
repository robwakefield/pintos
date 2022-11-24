#ifndef FRAME_H
#define	FRAME_H

#include <debug.h>
#include <hash.h>
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

#define BUCKET_COUNT 16

struct frame_entry
{
    void *frame_address;
    struct thread *owner;
    struct hash_elem elem;
};

void init_frame_table(void);

unsigned frame_hash (struct hash_elem *h, void *aux UNUSED);

bool frame_less_func (struct hash_elem *e1, struct hash_elem *e2, void *aux UNUSED);

void *frame_alloc (enum palloc_flags flags);
void frame_free (void *frame);

#endif
