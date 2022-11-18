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
  thread_exit();
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
  const char *cmd_line = (char*) valid_pointer (*(void**) get_argument (f, 0));
  tid_t child_tid = process_execute (cmd_line);
  f->eax = child_tid;
}

void
syscall_wait (struct intr_frame *f) {
  f->eax = process_wait (*(tid_t*) get_argument (f, 0));
}

struct file * table[FD_SIZE] = {0};

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
  struct fdTable *prevTable;
  struct fdPage *page;
  int free;
  struct file * table[FD_SIZE];
};

struct fdPage{
  struct list_elem elem;
  int free;
  struct fdTable tables[6];
};

static struct fdPage * newFilePage(){
  struct fdPage *page = palloc_get_page(PAL_ZERO);
  if(&page->elem == NULL){
    lock_release(&filesys_lock);
    exit_with_code(-1);
  }
  list_push_back(&file_page_list,&page->elem);
  page->free = 6;
  return page;
}

static struct fdTable* newFileTable(int tid){
  struct list_elem *e;
  for (e = list_begin (&file_page_list); e != list_end (&file_page_list); e = list_next (e)){
    struct fdPage *page = list_entry(e,struct fdTable, elem);
    if(page->free > 0){
      for(int i = 0; i<6;i++){
        struct fdTable *table = &(page->tables[i]);
        if (table->tid == 0){          
          table->tid = tid;
          table->tabNum = 0;
          table->nextTable = NULL;
          table->free = FD_SIZE;
          table->elem.prev == NULL;
          table->elem.next == NULL;
          table->page = page;
          table->prevTable = NULL; 
          memset(&(table->table),0,sizeof(table->table));
          page->free -= 1;
          return &table;
        }
      }
    }
  }
  struct fdPage *page = newFilePage();
  struct fdTable *table = &(page->tables[0]);
  table->tid = tid;
  table->tabNum = 0;
  table->nextTable = NULL;
  table->free = FD_SIZE;
  table->elem.prev == NULL;
  table->elem.next == NULL;
  table->page = page;
  table->prevTable = NULL;
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
  return NULL;
}

static int maxFileTab = 5;

static struct fdTable* extendFDTable(struct fdTable* table){
  ASSERT(table != NULL);
  if(table->tabNum == maxFileTab){
    return NULL;
  }

  struct fdTable *newTable = newFileTable(thread_current());
  table->nextTable = newTable;
  newTable -> prevTable = table;
  newTable->tabNum = table->tabNum + 1;
  return newTable;
}

int assign_fd(struct file *file){
  int fd = 2;
  struct fdTable *table = tidFileTable(thread_current());
  if(table == NULL){
    table = newFileTable(thread_current());
    if(table == NULL){
      return -1;
    }
    table->tabNum = 0;
    list_push_back(&file_list,&(table->elem));
  }
  ASSERT(table!=NULL);
  while(true){
    if(table->free > 0){
      for (int i = 0; i < FD_SIZE; i++) {
        if (table->table[i] == NULL) {

          table->table[i] = file;
          table->free -= 1;
          return fd + i;
        }
      }
      
    }
    if(table->nextTable == NULL){
        break;
      }
    fd += FD_SIZE;
    table = table->nextTable;
  }
  
  struct fdTable *tableT = extendFDTable(table);
  if (tableT == NULL){
    return -1;
  }
  tableT->table[0] == file;
  return fd;
}

struct file* fd_to_file(int i){
  int fd = i - 2;
  if(fd < 0){
    lock_release(&filesys_lock);
    exit_with_code (-1);
  }
  for(struct fdTable *table = tidFileTable(thread_current());(table != NULL);table = table->nextTable){
    if(fd < FD_SIZE){
      if(table->table[fd] != NULL){
        return table->table[fd];
      }else{
        lock_release(&filesys_lock);
        exit_with_code (-1);
        return NULL;
      }
    }
    i -= FD_SIZE;
  }
  lock_release(&filesys_lock);
  exit_with_code (-1);
}

void freeTable(struct fdTable *table){
  if(table == NULL){
    return;
  }
  if(table->free == FD_SIZE && table->nextTable == NULL){

    struct fdTable *prev = table->prevTable;
 
    table->page->free += 1;

    table->prevTable = NULL;

    if(table->tabNum == 0){
      list_remove(&table->elem);
    }
 
    if(table->page->free == 6){
  
      list_remove(&table->page->elem);

      palloc_free_page(table->page);

    }else{

      memset(table,0,sizeof(struct fdTable));
      cleanFileMemory(table);
    }

    if(prev != NULL){
      prev->nextTable = NULL;
      freeTable(prev);
    }
    

        
  }
}

void cleanFileMemory(struct fdTable *table){
  struct fdPage *page = list_entry(list_end(&file_page_list),struct fdPage, elem);
  struct fdTable *endTable;
  for (int i = 0; i < 6; i++){
    endTable = &page->tables[i];
    if(endTable->tid != 0){
      *table = *endTable;
      if(table->prevTable != NULL){
        table->prevTable->nextTable = table;
      }
      if(table->nextTable != NULL){
        table->nextTable->prevTable = table;
      }
      freeTable(endTable);
      return;
    }
  }
}

void remove_fd(int i){
  int fd = i - 2;
  if(fd < 0){
    lock_release(&filesys_lock);
    return;
  }
  for(struct fdTable *table = tidFileTable(thread_current());(table != NULL);table = table->nextTable){
    if(fd < FD_SIZE){
      table->table[fd] = NULL;
      table->free += 1;
      freeTable(table);
      return;
    }
    i -= FD_SIZE;
  }
  lock_release(&filesys_lock);
}

void closeProcess(int tid){
  for(struct fdTable *table = tidFileTable(tid);(table != NULL);){
    for(int i = 0; i < FD_SIZE && table->free < FD_SIZE ;i++){
      if (table->table[i] != NULL){
        file_close(table->table[i]);
        table->table[i] = NULL;
        table->free += 1;
      }
    }    
    table = table->nextTable;
    freeTable(table);
  }
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
  void *buffer = valid_pointer (*(void**) get_argument (f, 1));
  unsigned size = *(unsigned*) get_argument (f, 2);
  /* Ensure the entirety of buffer is valid */
  valid_pointer (buffer + size);
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
    
    struct file *file = fd_to_file (fd);
    if (file != NULL) {
      remove_fd(fd);
      file_close (file);
    }

    lock_release (&filesys_lock);
  }
}
