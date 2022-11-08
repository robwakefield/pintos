#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
static void (*syscall_handlers[20]) (struct intr_frame *);     /* Array of function pointers so syscall handlers. */
static bool valid_pointer (void *);

/* System call handler functions. */
/* do these need to be static or not? */
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

void
syscall_init (void) 
{
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

  /* check if pointer is valid */
  //in other branch

  /* get system call number */
  // type?
  int syscall_num = *(int*)f->esp;

  /* call syscall from array syscall_handlers */
  syscall_handlers[syscall_num](f);

  /* handle return value */
}

void *get_first_arg (struct intr_frame *f) {
  /* get f->esp + 4 */
}

/* Implement all syscalls needed for Task 2 - User Programs */
void
syscall_halt(struct intr_frame *f) {

}

void
syscall_exit(struct intr_frame *f) {
  
}

void
syscall_exec(struct intr_frame *f) {

}

void
syscall_create(struct intr_frame *f) {
  
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

/* Returns true if the pointer is a valid user pointer */
static bool
valid_pointer (void *p)
{
  if (pagedir_get_page (thread_current ()->pagedir, p) == NULL) {
    return false;
  }
  return is_user_vaddr (p);
}

void
exit (int status)
{
  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
}

int
write (int fd, const void *buffer, unsigned length)
{
  if (fd == 1) {
    putbuf (buffer, length);
  }
}
