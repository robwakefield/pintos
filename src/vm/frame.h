#ifndef FRAME_H
#define	FRAME_H

#include <hash.h>

struct frame_entry
{
    
    struct hash_elem elem;
};

void init_frame_table(void);

unsigned frame_hash ();

bool frame_less_func ();