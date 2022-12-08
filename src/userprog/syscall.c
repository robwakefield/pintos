#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "userprog/syscall.h"
#include "userprog/fdTable.h"

static void syscall_handler (struct intr_frame *);
static void (*syscall_handlers[20]) (struct intr_frame *);     /* Array of function pointers so syscall handlers. */
void exit_with_code (int);
static void *valid_pointer (void *, struct intr_frame *, bool);




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
void syscall_mmap (struct intr_frame *f);
void syscall_munmap (struct intr_frame *f);

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
  /* Call appropriate system call function from system calls array. */
  int syscall_num = *(int *) valid_pointer (f->esp, f, 0);

  if (syscall_num >= SYS_HALT && syscall_num <= SYS_MUNMAP) {
    syscall_handlers[syscall_num] (f);
  } else {
    exit_with_code (-1);
  } 
}


   /* Reads a byte at user virtual address UADDR.
      UADDR must be below PHYS_BASE.
      Returns the byte value if successful, -1 if a segfault
      occurred. */
static int
get_user (const uint8_t *uaddr)
{
     int result;
     asm ("movl $1f, %0; movzbl %1, %0; 1:"
          : "=&a" (result) : "m" (*uaddr));
     return result;
}
   /* Writes BYTE to user address UDST.
      UDST must be below PHYS_BASE.
      Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
     int error_code;
     asm ("movl $1f, %0; movb %b2, %1; 1:"
          : "=&a" (error_code), "=m" (*udst) : "q" (byte));
     return error_code != -1;
}

/* Returns true if the pointer is a valid user pointer */
void *valid_pointer (void *p, struct intr_frame *f, bool write)
{
  if (!is_user_vaddr (p)) {
    exit_with_code (-1);
  }
  thread_current ()->esp = f->esp;
  if (write) {
    int b = get_user (p);
    if (!put_user (p, b)) {
      //printf("exiting: bad write\n");
      exit_with_code (-1);
    }
  } else {
    if (get_user (p) == -1) {
      //printf("exiting: bad read\n");
      exit_with_code (-1);
    }
  }
  return p; 
}

void exit_with_code (int status) {
  thread_current ()->exit_status = status;
  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit();
}

/* Get ith argument */
void *get_argument (struct intr_frame *f, int i) {
  return valid_pointer(f->esp + (i + 1) * 4, f, 0);
}

void
syscall_halt (struct intr_frame *f UNUSED) {
  shutdown_power_off();
}

void
syscall_exit (struct intr_frame *f) {
  int status = *(int*) get_argument (f, 0);
  exit_with_code (status);
}

void
syscall_exec (struct intr_frame *f) {
  const char *cmd_line = (char*) valid_pointer (*(void**) get_argument (f, 0), f, 0);
  tid_t child_tid = process_execute (cmd_line);
  f->eax = child_tid;
}

void
syscall_wait (struct intr_frame *f) {
  f->eax = process_wait (*(tid_t*) get_argument (f, 0));
}









void
syscall_create (struct intr_frame *f) {
  const char *file = (const char*) valid_pointer (*(void**) get_argument (f, 0), f, 0);
  unsigned int initial_size = *(unsigned int*) get_argument (f, 1);

  if (file == NULL) {
    exit_with_code (-1);
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
  const char *name = (const char*) valid_pointer (*(void**) get_argument (f, 0), f, 0);
  if (name == NULL) {
    f->eax = -1;
    return;
  }
  lock_acquire (&filesys_lock);
  struct file *file = filesys_open (name);
  
  if (file == NULL) {
    f->eax = -1;
  } else {
    f->eax = assign_fd (file);
  }
  lock_release (&filesys_lock);
}

void
syscall_filesize (struct intr_frame *f) {
  int fd = *(int*) get_argument (f, 0);
  if(fd < 2){
    f->eax = 0;
  }else{
    lock_acquire (&filesys_lock);
    struct file *file = fd_to_file (fd);
    if (file != NULL) {
      f->eax = (int) file_length (file);
    }
    lock_release (&filesys_lock);
  }
}

void
syscall_read (struct intr_frame *f) {
  int fd = *(int*) get_argument (f, 0);
  void *buffer = valid_pointer (*(void**) get_argument (f, 1), f, 1);
  unsigned size = *(unsigned*) get_argument (f, 2);
  /* Ensure the entirety of buffer is valid */
  valid_pointer (buffer + size, f, 1);
  if (fd == STDIN_FILENO) {
    *(char*) buffer = input_getc();
    f->eax = 1;
  } else if (fd == 1) {
    f->eax = -1;
  } else {
    lock_acquire (&filesys_lock);
    struct file* file = fd_to_file (fd);
    if (file != NULL) {  
      f->eax = file_read (file, buffer, size);
    }
    lock_release (&filesys_lock);
  }
}

void
syscall_write (struct intr_frame *f) {
  int fd = *(int *) get_argument (f, 0);
  void *buffer = valid_pointer (*(void**) get_argument (f, 1), f, 0);
  unsigned size = *(unsigned *) get_argument (f, 2);
  
  /* Ensure the entirety of buffer is valid */
  valid_pointer (buffer + size, f, 0);

  if (fd == STDOUT_FILENO) {
    putbuf ((char *) buffer, size);
    f->eax = size;
  } else if (fd == 0) {
    f->eax = 0;
  } else {
    lock_acquire (&filesys_lock);
    struct file *file = fd_to_file (fd);
    if (file != NULL) {
      f->eax = file_write (file, buffer, size);
    }
    lock_release (&filesys_lock);
  }
}

void
syscall_seek (struct intr_frame *f) {
  int fd = *(int*) get_argument (f, 0);
  off_t position = *(off_t*) get_argument (f, 1);
  if (fd > 2) {
    lock_acquire (&filesys_lock);
    struct file *file = fd_to_file (fd);
    if (file != NULL) {
      file_seek (file, position);
    }
    lock_release (&filesys_lock);
  }
}

void
syscall_tell (struct intr_frame *f) {
  int fd = *(int*) get_argument (f, 0);
  if (fd < 2) {
    f->eax = 0;
  } else {
    lock_acquire (&filesys_lock);
    struct file *file = fd_to_file (fd);
    if (file != NULL) {
      f->eax = (unsigned) file_tell (file);
    }
    lock_release (&filesys_lock);
  }
}

void
syscall_close (struct intr_frame *f) {
  int fd = *(int*) get_argument (f, 0);
  if(fd > 2){
    lock_acquire (&filesys_lock);
    
    struct file *file = fd_to_file (fd);
    if (file != NULL) {
      remove_fd(fd);
      file_close (file);
    }

    lock_release (&filesys_lock);
  }
}

void syscall_mmap (struct intr_frame *f){
  
  int fd = *(int*) get_argument (f, 0);
  // unused variable
  //void *addr = valid_pointer (*(void**) get_argument (f, 1), f, 0);
  lock_acquire(&filesys_lock);
  struct file *file = fd_to_file (fd); 

  if (file == NULL){
    f->eax =  -1;
    lock_release (&filesys_lock);
    return;
  }

  struct file *new_file = file_reopen(file);
  if (new_file == NULL){
    f->eax =  -1;
    lock_release (&filesys_lock);
    return;
  }

  int size = file_length(new_file);
  void *buffer = palloc_get_multiple(PAL_USER,(size/PGSIZE) + 1);
  
  if (buffer == NULL) {
    f->eax = -1;
    file_close(new_file);
  } else {
    f->eax = assign_fd(new_file);
  }

  lock_release(&filesys_lock);
  
}

void syscall_munmap (struct intr_frame *f){
  int mapid = *(int*) get_argument (f, 0);
  lock_acquire(&filesys_lock);
  struct file *file = fd_to_file (mapid);
  file_close(file);
  lock_release(&filesys_lock);
}






