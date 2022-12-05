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
  struct hash_elem *removed = hash_delete (frame_table, &temp_entry->elem);
  /* TODO: what if hash_delete returns a NULL pointer. */

  free (temp_entry); 

  if (removed != NULL) {
    /* Deallocate the frame table entry. */
    struct frame_entry *f = hash_entry (removed, struct frame_entry, elem);
    free (f);
    lock_release (&frame_table_lock);

    palloc_free_page (pg_round_down (frame));
  }
}

/* Allocates a frame for the given page. */
void *
frame_alloc (enum palloc_flags flags) {
  lock_acquire(&frame_table_lock);

  void *f_page = palloc_get_page (PAL_USER | flags);

  if(f_page == NULL) {
    // page allocation failed
    // TODO: implement eviction
    lock_release (&frame_table_lock);
    return NULL;
  }

  struct frame_entry *frame = malloc (sizeof (struct frame_entry));
  if (frame == NULL) {
    // frame allocation failed. a critical state or panic?
    lock_release (&frame_table_lock);
    return NULL;
  }

  frame->owner = thread_current ();
  frame->frame_address = f_page;

  // insert into hash table
  if (hash_insert (frame_table, &frame->elem) != NULL) {
    free (frame);
    printf ("frame_alloc: frame already in frame table\n");
  }

  lock_release (&frame_table_lock);

  return f_page;
}

/* Frees all resources used by frame table entry
and removes entry from the frame table. */
void
frame_free (void *frame) {
  remove_frame (frame);
}
