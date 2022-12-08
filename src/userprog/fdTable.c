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
#include <string.h>

static struct list file_list;

void file_init(){
  list_init(&file_list);
}

static struct fdTable* new_file_table(int tid){
  
  struct fdTable *table = malloc(sizeof(struct fdTable));
  if (table == NULL){
    return NULL;
  }
  table->tid = tid;
  table->tabNum = 0;
  table->nextTable = NULL;
  table->free = FD_SIZE;
  table->elem.prev = NULL;
  table->elem.next = NULL;
  table->prevTable = NULL; 
  memset(&(table->table),0,sizeof(table->table));
  return table;
}

static struct fdTable* tid_file_table(int tid){
  struct list_elem *e;
  for (e = list_begin (&file_list); e != list_end (&file_list); e = list_next (e)){
    struct fdTable *table = list_entry(e,struct fdTable, elem);
    if(table->tid == tid){
      return table;
    }
  }
  return NULL;
}

static struct fdTable* extend_fd_table(struct fdTable* table){
  ASSERT(table != NULL);


  struct fdTable *newTable = new_file_table(thread_current()->tid);
  if (newTable == NULL) {
    return NULL;
  }
  table->nextTable = newTable;
  newTable -> prevTable = table;
  newTable->tabNum = table->tabNum + 1;
  return newTable;
}

static void free_table(struct fdTable *table){
  if(table == NULL){
    return;
  }
  if(table->free == FD_SIZE && table->nextTable == NULL){
    struct fdTable *prev = table->prevTable;
    table->prevTable = NULL;
    if(table->tabNum == 0){
      list_remove(&table->elem);
    }
    free(table);
    if(prev != NULL){
      prev->nextTable = NULL;
      free_table(prev);
    }
  }
}

int assign_fd(struct file *file){
  int fd = 2;
  struct fdTable *table = tid_file_table(thread_current()->tid);
  if(table == NULL){
    table = new_file_table(thread_current()->tid);
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
  
  struct fdTable *tableT = extend_fd_table(table);
  if (tableT == NULL){
    return -1;
  }
  tableT->table[0] = file;
  return fd;
}

struct file* fd_to_file(int i){
  int fd = i - 2;
  if(fd < 0){
    return NULL;
  }
  for(struct fdTable *table = tid_file_table(thread_current()->tid);(table != NULL);table = table->nextTable){
    if(fd < FD_SIZE){
      return table->table[fd];
    }
    fd -= FD_SIZE;
  }
  return NULL;
}

void remove_fd(int i){
  int fd = i - 2;
  if(fd < 0){
    return;
  }
  for(struct fdTable *table = tid_file_table(thread_current()->tid);(table != NULL);table = table->nextTable){
    if(fd < FD_SIZE){
      table->table[fd] = NULL;
      table->free += 1;
      free_table(table);
      return;
    }
    i -= FD_SIZE;
  }
}

void close_files(int tid){
  struct fdTable *table;
  for(table = tid_file_table(tid);(table != NULL);){
    for(int i = 0; i < FD_SIZE && table->free < FD_SIZE ;i++){
      if (table->table[i] != NULL){
        file_close(table->table[i]);
        table->table[i] = NULL;
        table->free += 1;
      }
    }    
    if (table->nextTable == NULL) {
      free_table(table);
      return;
    }
    table = table->nextTable;
  }
}


