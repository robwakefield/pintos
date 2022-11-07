#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
static bool valid_pointer (void *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  thread_exit ();
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
