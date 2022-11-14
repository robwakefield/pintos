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
syscall_halt(struct intr_frame *f) {
  shutdown_power_off();
}

void
syscall_exit(struct intr_frame *f) {
  int status = *(int*) get_arg (f, 0);
  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
}

void
syscall_exec(struct intr_frame *f) {

}

void
syscall_wait(struct intr_frame *f) {

}

struct file * fd_to_file(int fd){
  return (struct file *) fd;
}

int file_to_fd(struct file *file){
  return (int) file;
}

void
syscall_create(struct intr_frame *f) {
  const char *file = *(const char**) get_arg (f,0);
  off_t size = *(off_t*) get_arg (f,1);
  bool created = filesys_create(file,size);
  *f->eax = created;
}

void
syscall_remove(struct intr_frame *f) {
  const char *file = *(const char**) get_arg (f,0);
  bool removed = filesys_remove(file);
  *f->eax = removed;
}

void
syscall_open(struct intr_frame *f) {
  const char *file = *(const char**) get_arg (f,0);
  *f->eax = file_to_fd(file);
}

void
syscall_filesize(struct intr_frame *f) {
  int fd = *(int*) get_arg (f,0);
  struct file *file = fd_to_file(fd);
  *f->eax = (int) file_length(file);
}

void
syscall_read(struct intr_frame *f) {
  int fd = *(int*) get_arg (f,0);
  void *buffer = get_arg(f,1);
  off_t size = *(off_t*);
  *f->eax = (int) file_read(fd_to_file(fd),buffer,size);

}

void
syscall_write(struct intr_frame *f) {
  int fd = *(int*) get_arg (f, 0);
  const void* buffer = get_arg (f, 1);
  unsigned length = *(unsigned*) get_arg (f, 2);
  int r = 0;
  if (fd == 1) {
    putbuf (buffer, length);
    r = (int) lenght;
  }else if (fd != 0){
    r = (int) file_write(fd_to_file(fd), buffer,(off_t)length);
  }
  *f->eax = r;
}

void
syscall_seek(struct intr_frame *f) {
  int fd = *(int*) get_arg (f, 0);
  off_t position = *(off_t*) get_arg(f,1);
  file_seek(fd,position);
}

void
syscall_tell(struct intr_frame *f) {
  int fd = *(int*) get_arg (f, 0);
  unsigned position = (unsigned) file_tell(fd_to_file(fd));
  *f->eax = position;
}

void
syscall_close(struct intr_frame *f) {
  
}
