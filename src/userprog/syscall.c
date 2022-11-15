#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

static void syscall_handler (struct intr_frame *);
static void (*syscall_handlers[20]) (struct intr_frame *);     /* Array of function pointers so syscall handlers. */
static bool valid_pointer (void *);

void *get_argument (struct intr_frame *f, int i);

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
  lock_init (&filesys_lock);

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

  /* Get system call number. */
  int syscall_num = *(int *) f->esp;
  //printf ("system call! %d\n", syscall_num);

  /* Call appropriate system call function from system calls array. */
  syscall_handlers[syscall_num] (f);

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
void *get_argument (struct intr_frame *f, int i) {
  return f->esp + (i + 1) * 4;
  // TODO: check if its a valid pointer here?
}

/* Implement all syscalls needed for Task 2 - User Programs */
void
syscall_halt (struct intr_frame *f) {
  shutdown_power_off();
}

void
syscall_exit (struct intr_frame *f) {

  int status = (f == NULL) ? (-1) : *(int*) get_argument (f, 0);

  thread_current ()->exit_status = status;

  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
}

void
syscall_exec (struct intr_frame *f) {
  const char *cmd_line = get_argument (f, 0);

  if (!valid_pointer (cmd_line)) {
    syscall_exit (NULL);
  }

  lock_acquire (&filesys_lock);
  tid_t child_tid = process_execute (cmd_line);
  lock_release (&filesys_lock);
}

void
syscall_wait (struct intr_frame *f) {
  int ret = process_wait (get_argument (f, 0));

  f->eax = ret;
}

struct file * fd_to_file (int fd){
  return (struct file *) fd;
}

int file_to_fd (struct file *file){
  return (int) file;
}

void
syscall_create (struct intr_frame *f) {

  const char *file = get_argument (f, 0);
  unsigned int initial_size = get_argument (f, 1);

  if (file == NULL) {
    syscall_exit (NULL);
  }

  lock_acquire (&filesys_lock);
  bool success = filesys_create (file, initial_size);
  lock_release (&filesys_lock);

  f->eax = success;
}

void
syscall_remove (struct intr_frame *f) {
  const char *file = *(const char**) get_argument (f, 0);
  lock_acquire (&filesys_lock);
  bool removed = filesys_remove (file);
  lock_release (&filesys_lock);
  f->eax = removed;
}

void
syscall_open (struct intr_frame *f) {
  const char *name = *(const char**) get_argument (f, 0);
  lock_acquire (&filesys_lock);
  struct file *file = filesys_open (name);
  lock_release (&filesys_lock);
  f->eax = file_to_fd (file);

}

void
syscall_filesize (struct intr_frame *f) {
  int fd = *(int*) get_argument (f, 0);
  struct file *file = fd_to_file (fd);
  lock_acquire (&filesys_lock);
  f->eax = (int) file_length (file);
  lock_release (&filesys_lock);
}

void
syscall_read (struct intr_frame *f) {
  int fd = *(int*) get_argument (f, 0);
  void *buffer = get_argument (f, 1);
  off_t size = *(off_t*) get_argument (f, 2);
  lock_acquire (&filesys_lock);
  f->eax = (int) file_read (fd_to_file (fd), buffer, size);
  lock_release (&filesys_lock);
}

void
syscall_write (struct intr_frame *f) {
  int fd = *(int *) get_argument (f, 0);
  const void *buffer = *(void **) get_argument (f, 1);
  unsigned length = *(unsigned *) get_argument (f, 2);

  // TODO: re implement locking code here
  /* change to not use magic numbers. */
  if (fd == 1) {
    putbuf ((char *) buffer, length);
    f->eax = length;
  } else if (fd == 0) {
    f->eax = 0;
  }
}

void
syscall_seek (struct intr_frame *f) {
  int fd = *(int*) get_argument (f, 0);
  off_t position = *(off_t*) get_argument (f, 1);
  lock_acquire (&filesys_lock);
  file_seek (fd, position);
  lock_release (&filesys_lock);
}

void
syscall_tell (struct intr_frame *f) {
  int fd = *(int*) get_argument (f, 0);
  lock_acquire (&filesys_lock);
  unsigned position = (unsigned) file_tell (fd_to_file (fd));
  lock_release (&filesys_lock);
  f->eax = position;
}

void
syscall_close (struct intr_frame *f) {
  int fd = *(int*) get_argument (f, 0);
  lock_acquire (&filesys_lock);
  file_close (fd_to_file (fd));
  lock_release (&filesys_lock);
}
