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
#include <string.h>
#include "userprog/mapId.h"
static struct list file_list;

void mmap_init(){
  list_init(&file_list);
}


static struct mmapTable* new_mmap_table(){
  
  struct mmapTable *table = malloc(sizeof(struct mmapTable));
  if (table == NULL){
    return NULL;
  }
  table->tabNum = 0;
  table->nextTable = NULL;
  table->free = MM_SIZE;
  table->prevTable = NULL; 
  table->header = NULL;
  memset(&(table->table),0,sizeof(table->table));
  return table;
}

static struct mmapProc* tid_mmap_proc(int tid){
  struct list_elem *e;
  for (e = list_begin (&file_list); e != list_end (&file_list); e = list_next (e)){
    struct mmapProc *table = list_entry(e,struct mmapProc, elem);
    if(table->tid == tid){
      return table;
    }
  }
  return NULL;
}

static struct mmapTable* extend_mmap_table(struct mmapTable* table){
  ASSERT(table != NULL);


  struct mmapTable *newTable = new_mmap_table();
  if (newTable == NULL) {
    return NULL;
  }
  table->nextTable = newTable;
  newTable -> prevTable = table;
  newTable->tabNum = table->tabNum + 1;
  newTable->header = table->header;
  return newTable;
}

static void free_table(struct mmapTable *table){
  if(table == NULL){
    return;
  }
  if(table->free == MM_SIZE && table->nextTable == NULL){
    struct mmapTable *prev = table->prevTable;
    table->prevTable = NULL;
    if(table->tabNum == 0){
      struct mmapProc *proc = table->header;
      list_remove(&proc->elem);
      
      free(proc);
    }
    free(table);
    if(prev != NULL){
      prev->nextTable = NULL;
      free_table(prev);
    }
  }
}

int assign_mapId(void *addr){
  int mapId = 0;
  struct mmapProc *proc = tid_mmap_proc(thread_current()->tid);
  struct mmapTable *table;
  if(proc == NULL){
    proc = malloc(sizeof(struct mmapProc));
    if(proc == NULL){
      return -1;
    }
    table = new_mmap_table(thread_current()->tid);
    if(table == NULL){
      free(proc);
      return -1;
    }
    table->tabNum = 0;
    table->header = proc;
    proc->tid = thread_current()->tid;
    proc->mmapTable = table;
    list_push_back(&file_list,&(proc->elem));
  }else{
    table = proc->mmapTable;
  }
  ASSERT(table!=NULL);
  while(true){
    if(table->free > 0){
      for (int i = 0; i < MM_SIZE; i++) {
        if (table->table[i] == NULL) {
          table->table[i] = addr;
          table->free -= 1;
          return mapId + i;
          
        }
      }
    }
    if(table->nextTable == NULL){
        break;
      }
    mapId += MM_SIZE;
    table = table->nextTable;
  }
  
  struct mmapTable *tableT = extend_mmap_table(table);
  if (tableT == NULL){
    return -1;
  }
  tableT->table[0] = addr;
  return mapId;
}

void* mapId_to_file(int i){
  int mapId = i;
  if(mapId < 0){
    return NULL;
  }
  struct mmapProc *proc = tid_mmap_proc(thread_current()->tid);
  for(struct mmapTable *table = proc->mmapTable;(table != NULL);table = table->nextTable){
    if(mapId < MM_SIZE){
      return table->table[mapId];
    }
    mapId -= MM_SIZE;
  }
  return NULL;
}

void remove_mapId(int i){
  int mapId = i;
  if(mapId < 0){
    return;
  }
  struct mmapProc *proc = tid_mmap_proc(thread_current()->tid);
  for(struct mmapTable *table = proc->mmapTable;(table != NULL);table = table->nextTable){
    if(mapId < MM_SIZE){
      table->table[mapId] = NULL;
      table->free += 1;
      free_table(table);
      return;
    }
    i -= MM_SIZE;
  }
}

void close_mapId(int tid){
  int a = 0;
  struct mmapProc *proc = tid_mmap_proc(thread_current()->tid);
  if(proc == NULL){
    return;
  }
  struct file *file;
  for(struct mmapTable *table = proc->mmapTable;(table != NULL);){
    for(int i = 0; i < MM_SIZE && table->free < MM_SIZE ;i++){
      if (table->table[i] != NULL){
        struct page *page = page_lookup (thread_current ()->page_table, mapId_to_file(i + a));
        file = page->file;
        unmmap(i+a);
        file_close(file);

        table->table[i] = NULL;
        table->free += 1;
      }
    }    
    if (table->nextTable == NULL) {
      free_table(table);
      return;
    } 
    table = table->nextTable;
    a += MM_SIZE;
  }
}


