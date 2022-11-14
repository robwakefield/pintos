#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);
static void (*syscall_handlers[20]) (struct intr_frame *);     /* Array of function pointers so syscall handlers. */
static bool valid_pointer (void *);

void get_arguments(struct intr_frame *f, int *args, int n);

/* System call handler functions. */
void syscall_halt (struct intr_frame *);
void syscall_exit (struct intr_frame *);
void syscall_exec (struct intr_frame *);
void syscall_wait (struct intr_frame *);
void syscall_create (struct intr_frame *);
void syscall_remove (struct intr_frame *);
void syscall_open (struct intr_frame *);
void syscall_filesize (struct intr_frame *);
void syscall_read (struct intr_frame *);
void syscall_write (struct intr_frame *);
void syscall_seek (struct intr_frame *);
void syscall_tell (struct intr_frame *);
void syscall_close (struct intr_frame *);

/* Lock for the file system, ensures multiple processes cannot edit file at the same time. */
struct lock filesys_lock;

void
syscall_init (void) 
{
  lock_init(&filesys_lock);

  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  
  syscall_handlers[SYS_HALT] = &syscall_halt;
  syscall_handlers[SYS_EXIT] = &syscall_exit;
  syscall_handlers[SYS_EXEC] = &syscall_exec;
  syscall_handlers[SYS_WAIT] = &syscall_wait;
  syscall_handlers[SYS_CREATE] = &syscall_create;
  syscall_handlers[SYS_REMOVE] = &syscall_remove;
  syscall_handlers[SYS_OPEN] = &syscall_open;
  syscall_handlers[SYS_FILESIZE] = &syscall_filesize;
  syscall_handlers[SYS_READ] = &syscall_read;
  syscall_handlers[SYS_WRITE] = &syscall_write;
  syscall_handlers[SYS_SEEK] = &syscall_seek;
  syscall_handlers[SYS_TELL] = &syscall_tell;
  syscall_handlers[SYS_CLOSE] = &syscall_close;
}

static void
syscall_handler (struct intr_frame *f) 
{
  printf ("system call!\n");

  /* get system call number */
  int syscall_num = *(int*)f->esp;

  /* call syscall from array syscall_handlers */
  syscall_handlers[syscall_num](f);

  /* handle return value in eax*/
}

/* Returns true if the pointer is a valid user pointer */
static bool
valid_pointer (void *p)
{
  if (pagedir_get_page (thread_current ()->pagedir, p) == NULL) {
    return false;
  }
  return is_user_vaddr (p);
  /* TODO: handle invalid pointer (terminate thread) */
}

/* Get ith argument */
void *get_arg (struct intr_frame *f, int i) {
  void *a = f->esp + (4 * (i + 1));
  if (valid_pointer (a)) {
    return a;
  }
  // return valid_pointer (a) ?
}

/* Implement all syscalls needed for Task 2 - User Programs */
void
syscall_halt (struct intr_frame *f) {
  shutdown_power_off();
}

void
syscall_exit (struct intr_frame *f) {
  /* Set status = get_argument (f, 0); */
  int status = 0;

  thread_current ()->exit_status = status;

  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
}

void
syscall_exec(struct intr_frame *f) {
  const char *cmd_line = get_argument (f, 0);

  if (!valid_pointer (cmd_line)) {
    syscall_exit(-1);
  }

  lock_acquire (&filesys_lock);
  tid_t child_tid = process_execute (cmd_line);
  lock_release (&filesys_lock);
}

void
syscall_wait (struct intr_frame *f) {
  /* check if first arg is tid_t ?? */
  return process_wait (get_argument (f, 0));
}

void
syscall_create (struct intr_frame *f) {

  const char *file = get_argument (f, 0);
  unsigned int initial_size = get_argument (f, 1);

  if (file == NULL) {
    sys_exit(f);
  }

  lock_acquire (&filesys_lock);
  bool ret = filesys_create(file, initial_size);
  lock_release (&filesys_lock);

  // should we return bool based on success of filesys_create ??
  
}

void
syscall_remove(struct intr_frame *f) {
  
}

void
syscall_open(struct intr_frame *f) {
  
}

void
syscall_filesize(struct intr_frame *f) {
  
}

void
syscall_read(struct intr_frame *f) {
  
}

void
syscall_write(struct intr_frame *f) {
  int fd = *(int*) get_arg (f, 0);
  const void* buffer = get_arg (f, 1);
  unsigned length = *(unsigned*) get_arg (f, 2);
  if (fd == 1) {
    putbuf (buffer, length);
  }
}

void
syscall_seek(struct intr_frame *f) {
  
}

void
syscall_tell(struct intr_frame *f) {
  
}

void
syscall_close(struct intr_frame *f) {
  
}
