#ifndef FRAME_H
#define	FRAME_H

#include <hash.h>

#define BUCKET_COUNT 16

struct frame_entry
{
    void *frame_address;
    struct thread *owner;
    struct hash_elem elem;
};

void init_frame_table(void);

unsigned frame_hash (struct hash *elem);

bool frame_less_func ();
