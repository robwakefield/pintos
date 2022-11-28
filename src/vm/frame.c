#include "frame.h"

static struct hash *frame_table;
static struct lock frame_table_lock;

/* Initialization code for frame table. */
void
frame_table_init (void) {
  lock_init (&frame_table_lock);
  frame_table = malloc (sizeof (struct hash));
  hash_init (frame_table, frame_hash, frame_less_func, NULL);
}

/* Hash function for frame table. */
unsigned
frame_hash (const struct hash_elem *e, void *aux UNUSED) {
  const struct frame_entry *entry = hash_entry (e, struct frame_entry, elem);
  return hash_bytes (&entry->frame_address, sizeof (entry->frame_address));
}

/* Comparing function for frame table. */
bool
frame_less_func (const struct hash_elem *e1, const struct hash_elem *e2, void *aux UNUSED) {
  const struct frame_entry *a = hash_entry (e1, struct frame_entry, elem);
  const struct frame_entry *b = hash_entry (e2, struct frame_entry, elem);
  return a->frame_address < b->frame_address;
}

/* Creates a new frame table entry, which stores the address of the frame, 
the process that owns the frame. */
struct frame_entry *
create_entry (void *frame) {
  struct frame_entry *f = malloc (sizeof (struct frame_entry));
  f->owner = thread_current ();
  f->frame_address = frame;
  return f;
}

/* Creates a "fake" temporary hash element on the stack,
containing just enough information for hash_find to find the element we are looking for. */
struct frame_entry *
search_elem (void *address) {
  struct frame_entry *temp_entry;
  temp_entry = malloc (sizeof (struct frame_entry));
  temp_entry->frame_address = address;
  return temp_entry;
}

/* Adds a given frame table entry to the frame table. */
void
add_frame (void *frame) {
  lock_acquire (&frame_table_lock);

  struct frame_entry *entry = create_entry (frame); 
  hash_insert (frame_table, &entry->elem);
  
  lock_release (&frame_table_lock);

}

/* Removes given frame table entry from the frame table. */
void
remove_frame (void *frame) {
  lock_acquire (&frame_table_lock);

  struct frame_entry *temp_entry = search_elem (frame);
  hash_delete (frame_table, &temp_entry->elem);

  free (temp_entry); 
  lock_release (&frame_table_lock);
}

/* Allocates a frame for the given page. */
void *
frame_alloc (enum palloc_flags flags) {
  void *f = palloc_get_page (PAL_USER | flags);
  if (f == NULL) {
    ASSERT(false);
  }
  add_frame (f);
  return f;

}

/* Frees all resources used by frame table entry
and removes entry from the frame table. */
void
frame_free (void *frame) {
  palloc_free_page (frame);
  remove_frame (frame);
}