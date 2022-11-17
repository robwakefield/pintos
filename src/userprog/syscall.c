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

static void syscall_handler (struct intr_frame *);
static void (*syscall_handlers[20]) (struct intr_frame *);     /* Array of function pointers so syscall handlers. */
void exit_with_code (int);
static void *valid_pointer (void *);

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
  /* Call appropriate system call function from system calls array. */
  int syscall_num = *(int *) valid_pointer (f->esp);
  syscall_handlers[syscall_num] (f);
}

/* Returns true if the pointer is a valid user pointer */
void *valid_pointer (void *p)
{
  if (!is_user_vaddr (p) || pagedir_get_page (thread_current ()->pagedir, p) == NULL) {
    exit_with_code (-1);
  }
  return p; 
}

void exit_with_code (int status) {
  thread_current ()->exit_status = status;
  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
}

/* Get ith argument */
void *get_argument (struct intr_frame *f, int i) {
  return valid_pointer(f->esp + (i + 1) * 4);
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
  // TODO: inefficient casting
  const char *cmd_line = (char*) valid_pointer (*(void**) get_argument (f, 0));
  tid_t child_tid = process_execute (cmd_line);
  f->eax = child_tid;
}

void
syscall_wait (struct intr_frame *f) {
  f->eax = process_wait (*(tid_t*) get_argument (f, 0));
}

struct file * table[32] = {0};

static struct list file_list;
static struct list file_page_list;

void file_init(){
  list_init(&file_list);
  list_init(&file_page_list);
}

struct fdTable{
  int tid;
  int tabNum;
  struct list_elem elem;
  struct fdTable *nextTable;
  int free;
  struct file * table[32];
};

struct fdPage{
  struct list_elem elem;
  int free;
  struct fdTable tables[6];
};

static struct fdPage * newFilePage(){
  struct fdPage *page = palloc_get_page(PAL_ZERO);
  list_push_back(&file_page_list,&page->elem);
  page->free = 63;
  return page;
}

static struct fdTable* newFileTable(int tid){
  struct list_elem *e;
  for (e = list_begin (&file_page_list); e != list_end (&file_page_list); e = list_next (e)){
    struct fdPage *page = list_entry(e,struct fdTable, elem);
    if(page->free > 0){
      for(int i = 0; i<6;i++){
        if ((1<<i)&page->free == 1){
          struct fdTable *table = &(page->tables[i]);
          table->tid = tid;
          table->nextTable = NULL;
          table->free = 6;
          table->elem.prev == NULL;
          table->elem.next == NULL;
          memset(&(table->table),0,sizeof(table->table));
          page->free -= 1<<i;
          return &table;
        }
      }
    }
  }
  struct fdPage *page = newFilePage();
  struct fdTable *table = &(page->tables[0]);
  table->tid = tid;
  table->nextTable = NULL;
  table->free = 6;
  table->elem.prev == NULL;
  table->elem.next == NULL;
  memset(&(table->table),0,sizeof(table->table));
  page->free -= 1;
  return table;
}

static struct fdTable* tidFileTable(int tid){
  struct list_elem *e;
  for (e = list_begin (&file_list); e != list_end (&file_list); e = list_next (e)){
    struct fdTable *table = list_entry(e,struct fdTable, elem);
    if(table->tid == tid){
      return table;
    }
  }
  struct fdTable *table = newFileTable(tid);
  table->tabNum = 0;


  list_push_back(&file_list,&(table->elem));
  return &table;
}

static struct fdTable* extendFDTable(struct fdTable* table){
  ASSERT(table->nextTable == NULL);
  struct fdTable *newTable = newFileTable(table->tid);
  table->nextTable = newTable;
  return newTable;
}

static int assign_fd(struct file *file){
  int fd = 2;
  for(struct fdTable *table = tidFileTable(thread_current());(table != NULL);table = table->nextTable){
    if(table->free > 0){
      for (int i = 0; i < 32; i++) {
        if (table->table[i] == NULL) {

          table->table[i] = file;
          return fd + i;
        }
      }
    }
    fd += 32;
  }
  struct fdTable *tableT = extendFDTable(table);
  tableT->table[0] == file;
  return fd;

}



struct file *fd_to_file (int fd){
  if(fd < 2 || fd>=34 || table[fd - 2] == NULL){
    lock_release(&filesys_lock);
    exit_with_code (-1);


    return NULL;
  } else {
    return table[fd - 2];
  }
}


int file_to_fd (struct file *file) {
  assign_fd(file);
  for (int i = 0; i < 32; i++) {
    if (table[i] == NULL) {

      table[i] = file;
      return i + 2;
    }
  }
  return -1;
}


void remove_fd(int fd){
  table[fd - 2] = NULL;
}





void
syscall_create (struct intr_frame *f) {
  const char *file = (const char*) valid_pointer (*(void**) get_argument (f, 0));
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
  const char *name = (const char*) valid_pointer (*(void**) get_argument (f, 0));
  if (name == NULL) {
    f->eax = -1;
    return;
  }
  lock_acquire (&filesys_lock);
  struct file *file = filesys_open (name);
  lock_release (&filesys_lock);
  if (file == NULL) {
    f->eax = -1;
  } else {
    f->eax = file_to_fd (file);
  }
}

void
syscall_filesize (struct intr_frame *f) {
  int fd = *(int*) get_argument (f, 0);
  if(fd < 2){
    f->eax = 0;
  }else{
    struct file *file = fd_to_file (fd);
    
    lock_acquire (&filesys_lock);
    f->eax = (int) file_length (file);
    lock_release (&filesys_lock);
  }
}

void
syscall_read (struct intr_frame *f) {
  int fd = *(int*) get_argument (f, 0);
  void *buffer = valid_pointer (*(void**) get_argument (f, 1));
  unsigned size = *(unsigned*) get_argument (f, 2);
  /* Ensure the entirety of buffer is valid */
  valid_pointer (buffer + size);
  // TODO: remove magic numbers
  if (fd == 0) {
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
  const void *buffer = valid_pointer (*(void**) get_argument (f, 1));
  unsigned size = *(unsigned *) get_argument (f, 2);
  /* Ensure the entirety of buffer is valid */
  valid_pointer (buffer + size);

  /* TODO: change to not use magic. */
  if (fd == 1) {
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

    file_close (fd_to_file (fd));
    

    struct file *file = fd_to_file (fd);
    if (file != NULL) {
      file_close (file);
      remove_fd(fd);
    }

    lock_release (&filesys_lock);
  }
}
