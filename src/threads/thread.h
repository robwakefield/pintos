#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include <stdbool.h>
#include "synch.h"

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                         /* Thread identifier. */
    enum thread_status status;         /* Thread state. */
    char name[16];                     /* Name (for debugging purposes). */
    uint8_t *stack;                    /* Saved stack pointer. */
    int priority;                      /* Priority (Effective Priority) */
    int base_priority;                 /* Base Priority */
    int nice;                          /* Nice value for advanced scheduler */
    int recent_cpu;
    struct list_elem allelem;          /* List element for all threads list. */

    struct lock *waiting_on;           /* The lock currently blocking the thread. */
    struct list locks;                 /* List of locks the thread is currently holding. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;             /* List element. */ 

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                 /* Page directory. */

    struct thread *parent;             /* Records parent's thread ID. */

    struct list child_list;            /* List of all child threads of the process. */
    struct list_elem child_elem;       /* For storing child thread in the parent's list of children. */

    int exit_status;

    struct list open_fds;              /* List of open file descriptors of the process. */

    struct semaphore sema_wait;        /* Semaphore to be used when process is waiting. */
    struct semaphore sema_load;        /* Semaphore to block when parent is waiting for child to load. */
    struct semaphore sema_exit;        /* Synchronise removing child thread from parent's list of children. */

#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
   };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "mlfqs". */
extern bool thread_mlfqs;

extern int load_avg;

void calculate_priority(struct thread*, void *aux UNUSED);
void calculate_recent_cpu(struct thread*, void *aux UNUSED);
void calculate_load_avg(void);

void thread_init (void);
void thread_start (void);
size_t threads_ready(void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);
void donate (struct thread *t, int new_priority);

bool compare_priority(const struct list_elem *first, const struct list_elem *second, void *aux UNUSED);

void add_to_ready_list (struct thread *t);
void thread_add_lock (struct lock *lock);
void thread_remove_lock (struct lock *lock);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);
void thread_forin (thread_action_func *, struct list*, void *);

void donate (struct thread* t, int new_priority);
void revoke_donation (void);
bool test_yield (void);
void list_resort (struct list *list, struct list_elem *elem, list_less_func *less);
int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

#endif /* threads/thread.h */
