#include "frame.h"

static struct hash *frame_table;
static struct lock frame_table_lock;

void frame_table_init (void) {
  lock_init (&frame_table_lock);
  frame_table = malloc (sizeof (struct hash));
  hash_init (frame_table, frame_hash, frame_less_func, NULL);
}

unsigned frame_hash (const struct hash_elem *e, void *aux UNUSED) {
  struct frame_entry *entry = hash_entry (e, struct frame_entry, elem);
  return ((int) entry->frame_address / PGSIZE) % BUCKET_COUNT;
}

bool frame_less_func (const struct hash_elem *e1, const struct hash_elem *e2, void *aux UNUSED) {
  struct frame_entry *a = hash_entry (e1, struct frame_entry, elem);
  struct frame_entry *b = hash_entry (e2, struct frame_entry, elem);
  return a->frame_address < b->frame_address;
}

struct frame_entry *create_entry (void *frame) {
  struct frame_entry *f = malloc (sizeof (struct frame_entry));
  f->owner = thread_current ();
  f->frame_address = frame;
  return f;
}

struct frame_entry *search_elem (void *address) {
  struct frame_entry *temp_entry;
  temp_entry = malloc (sizeof (struct frame_entry));
  temp_entry->frame_address = address;
  return temp_entry;
}

void add_frame (void *frame) {
  lock_acquire (&frame_table_lock);
  struct frame_entry *entry = create_entry (frame); 
  hash_insert (frame_table, &entry->elem);
  lock_release (&frame_table_lock);
}

void remove_frame (void *frame) {
  lock_acquire (&frame_table_lock);
  struct frame_entry *temp_entry = search_elem (frame);
  hash_delete (frame_table, &temp_entry->elem);
  free (temp_entry); 
  lock_release (&frame_table_lock);
}

void *frame_alloc (enum palloc_flags flags) {
  void *f = palloc_get_page (PAL_USER | flags);
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
