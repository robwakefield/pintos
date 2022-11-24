#include "frame.h"

static struct hash *frame_table;
static struct lock *frame_table_lock;

void init_frame_table (void) {
  frame_table = malloc (sizeof (struct hash));
  lock_init (&frame_table_lock);
  hash_init (frame_table, frame_hash, frame_less_func);
}

unsigned frame_hash (struct hash *elem) {
  return (elem->frame_address / PGSIZE) % BUCKET_COUNT;
}

bool frame_less_func (struct hash *e1, struct hash *e2) {
  return e1->frame_address > e2->frame_address;
}

struct frame_entry *create_entry (void *frame) {
  struct frame_entry *f = malloc (sizeof (struct frame_entry));
  f->owner = thread_current ();
  f->frame_address = frame;
  return f;
}

struct hash_elem *search_elem (void *address) {
  struct hash_elem *temp_elem;
  temp_elem->frame_address = frame;
  return temp_elem;
}

void add_frame (void *frame) {
  lock_acquire (&frame_table_lock);
  struct frame_entry *entry = create_entry (frame); 
  hash_insert (frame_table, entry->elem);
  lock_release (&frame_table_lock);
}

void remove_frame (void *frame) {
  lock_acquire (&frame_table_lock);
  hash_delete (frame_table, search_elem (frame)); 
  lock_release (&frame_table_lock);
}

void *frame_alloc (enum palloc_flags) {
  void *f = palloc_get_page (PAL_USER | palloc_flags);
  if (f == NULL) {
    ASSERT(false);
  }
  add_frame (f);
  return f;
}

void frame_free (void *frame) {
  palloc_free_page (frame);
  remove_frame (frame);
}
