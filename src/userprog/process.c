#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "userprog/fdTable.h"
#include "userprog/mapId.h"

/* Passed argument struct */
struct arguments {
  char *fn_copy;
  int argc;
  char *argv[PGSIZE / 2];
};

static thread_func start_process NO_RETURN;
static bool load (const struct arguments *args, void (**eip) (void), void **esp);
static void *push_args_on_stack (const struct arguments *args);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
int count = 0;
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;
  
  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = frame_alloc (0, NULL);

  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Parse command line input into program name and arguments. */
  struct arguments *args;
  args = frame_alloc (PAL_ZERO, NULL);
  if (args == NULL) {
    frame_free (fn_copy, true);
    return TID_ERROR;
  }
  args->fn_copy = fn_copy;

  char *arg_val, *s_ptr;
  int args_size = 0; /* Ensure arguments are not too large to be placed on the stack */

  for (arg_val = strtok_r (fn_copy, " ", &s_ptr); arg_val != NULL; arg_val = strtok_r (NULL, " ", &s_ptr)) {
    /* Check arguments will fit on stack 
     * 4 bytes are reserved for word-align, argv, argc, return address */
    if ((args_size + strlen (arg_val) + 1) + 4 * (args->argc + 1) + 4 > PGSIZE) {
      frame_free (fn_copy, true);
      frame_free (args, true);
      return TID_ERROR;
    }
    args->argv[args->argc] = arg_val;
    args->argc++;
    args_size += strlen (arg_val) + 1;
  }

  /* Create a new thread to execute FILE_NAME. */
  char *prog_name = args->argv[0]; 
  tid = thread_create (prog_name, PRI_DEFAULT, start_process, args);

  if (tid != TID_ERROR) {
    /* Block parent until load is successful. */
    sema_down (&thread_current ()->sema_load);
  }

  if (!thread_current ()->load_status) {
    return TID_ERROR;
  } 
  
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *aux)
{
  struct arguments *args = (struct arguments *) aux;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (args, &if_.eip, &if_.esp);

  frame_free (args->fn_copy, true);
  frame_free (args, true);

  struct thread *curr = thread_current ();

  curr->parent->load_status = success;

  if (!success) {
    curr->exit_status = -1;
    sema_up (&curr->parent->sema_load);
    exit_with_code (-1);
  } else {
    sema_up (&curr->parent->sema_load);
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status. 
 * If it was terminated by the kernel (i.e. killed due to an exception), 
 * returns -1.  
 * If TID is invalid or if it was not a child of the calling process, or if 
 * process_wait() has already been successfully called for the given TID, 
 * returns -1 immediately, without waiting.
 * 
 * This function will be implemented in task 2.
 * For now, it does nothing. */
int
process_wait (tid_t child_tid) 
{
  struct thread *curr = thread_current ();
  struct thread *child = NULL;
  struct list_elem *elem;

  if(list_empty(&curr->child_list)) {
    return -1;
  }
  
  /* Find child thread in the child list of the parent thread. */
  for (elem = list_begin (&curr->child_list); 
       elem != list_end (&curr->child_list); 
       elem = list_next (elem))
  {
      struct thread *temp = list_entry (elem, struct thread, child_elem);
      
      if (temp->tid == child_tid) {
        child = temp;
	      break;
      }
  }

  if (child == NULL || child->parent != curr || child->waited) {
    return -1;
  }

  child->waited = true;

  /* Block parent thread until child exits. */
  sema_down (&child->sema_wait);

  /* Remove child from parent's child_list so it cannot be waited on again. */
  list_remove (&child->child_elem);
  sema_up (&child->sema_exit);

  return child->exit_status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  lock_acquire(&filesys_lock);
  close_files(thread_current()->tid);
  close_mapId(thread_current()->tid);
  lock_release(&filesys_lock);

  /* Once process exits, stop all children threads waiting blocked on sema_exit. */
  struct list_elem *elem;
  for (elem = list_begin (&cur->child_list); elem != list_end (&cur->child_list);
     elem = list_next (elem))
  {
    struct thread *temp = list_entry (elem, struct thread, child_elem);
    sema_up(&temp->sema_exit);
  }


  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  /* Destroy the page hash table. */
  page_table_destroy ();
  cur->page_table = NULL;
  
  sema_up (&cur->sema_wait);

  /* Wait for parent to remove child from its list of child threads before terminating. */
  sema_down (&cur->sema_exit);
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (const struct arguments *args, void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const struct arguments *args, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  
  if (args->argc == 0) {
    goto done;
  }
  char *file_name = args->argv[0]; 

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Create page hash table for current thread. */
  t->page_table = malloc (sizeof (struct hash));
  if (t->page_table == NULL) {
    goto done;
  }
  hash_init (t->page_table, page_hash, page_less, NULL);

  /* Open executable file. */
  lock_acquire(&filesys_lock);
  file = filesys_open (file_name);

  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Deny writes to open file */
  file_deny_write (file);
  int fd = assign_fd (file);
  if (fd == -1 ){
    goto done;
  }
  

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))          
                goto done;
            }
          else
            goto done;
          break;
        }
    }


  /* Set up stack. */
  if (!setup_stack (args, esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  lock_release(&filesys_lock);
  /* We arrive here whether the load is successful or not. */
  if (!success) {
    lock_acquire(&filesys_lock);
    file_close (file);
    remove_fd (fd);
    lock_release(&filesys_lock);
  }

  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  /* Note: filesys_lock should already be acquired here */
  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      page_alloc_with_file (thread_current ()->page_table, upage, file, ofs, page_read_bytes, 
                            page_zero_bytes, writable);
      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      ofs += page_read_bytes;
      upage += PGSIZE;
    }

  return true;

}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (const struct arguments *args, void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = frame_alloc (PAL_ZERO, ((uint8_t *) PHYS_BASE) - PGSIZE);

  if (kpage != NULL) 
    {
      frame_set_pinned (kpage, true);
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success) {
        *esp = push_args_on_stack (args);
      } else {
        frame_free (kpage, true);

      }
      
    }

  return true;
}

bool
grow_stack (void *vaddr) 
{
  vaddr = pg_round_down (vaddr);
  // TODO: ensure this is correct
  if (vaddr < ((uint8_t *) PHYS_BASE) - MAX_STACK_SIZE) {
    return false;
  }

  void *kpage = frame_alloc (PAL_ZERO, vaddr);
  if (kpage == NULL) {
    printf ("Couldn't grow stack: frame table is full!\n");
    return false;
  }

  struct page *p = page_alloc_zeroed (thread_current ()->page_table, vaddr);
  if (p == NULL) {
    frame_free (kpage, true);
    return false;
  }
  p->kpage = kpage;
  p->writable = true;
  p->status = IN_FRAME;
  add_to_pages (kpage, p);

  if (!install_page (p->addr, kpage, true)) {
    frame_free (kpage, true);
    free (p);
    printf ("Unable to grow stack!\n");
    return false;
  }
  return true;
}

static void *push_args_on_stack (const struct arguments *args) {
  void *esp = PHYS_BASE;
  char *arg_pointer[args->argc];
  arg_pointer[args->argc] = 0; /* NULL pointer sentinel */
  
  /* Push arguments on to stack */
  for (int i = args->argc - 1; i >= 0; i--) {
    size_t len = strlen (args->argv[i]) + 1;
    esp -= len;
    strlcpy (esp, args->argv[i], len);
    arg_pointer[i] = esp;
  }
  
  /* Round esp down to multiple of 4 */
  esp -= ((uint32_t) esp) % 4;

  /* Push address of each argument (RTL) */
  for (int i = args->argc; i >= 0; i--) {
    esp -= sizeof (char *);
    memcpy (esp, &arg_pointer[i], sizeof (char *));
  }
  
  /* Push argv */
  void *argv_p = esp;
  esp -= sizeof (char **);
  memcpy (esp, &argv_p, sizeof (char **));
  /* Push argc */
  esp -= sizeof (uint32_t);
  memcpy (esp, &args->argc, sizeof (uint32_t));
  /* Push a fake return address */
  esp -= sizeof (void *);
  
  return esp;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

bool load_file_page (struct page *p, void *kpage) {

  ASSERT (kpage != NULL);

  /* Check if virtual page already allocated. */
  struct thread *t = thread_current ();
  uint8_t *frame = pagedir_get_page (t->pagedir, p->addr);
      
  if (frame == NULL){
    /* Add the page to the process's address space. */
    if (!install_page (p->addr, kpage, p->writable)) {
      frame_free (kpage, true);
      return false; 
    }     

  } else {    
    /* Check if writable flag for the page should be updated */
    if(p->writable && !pagedir_is_writable(t->pagedir, p->addr)){
      pagedir_set_writable(t->pagedir, p->addr, p->writable); 
    }      
  }

  bool file_locked = lock_held_by_current_thread (&filesys_lock);
  if (!file_locked) {
    lock_acquire (&filesys_lock);
  }

  /* Load data into the page. */
  if(!load_file (kpage, p)) {
    if (!file_locked) {
      lock_release (&filesys_lock);
    }
    frame_free (kpage, true);
    return false;
  }

  if (!file_locked) {
    lock_release (&filesys_lock);
  }

  p->kpage = kpage;
  p->status = IN_FRAME;
  add_to_pages (kpage, p);

  frame_set_pinned (kpage, false);
  pagedir_set_dirty (t->pagedir, kpage, false);

  return true;
}

bool
load_page(struct hash *pt, uint32_t *pagedir, struct page *p)
{
  if (p->status == IN_FRAME) {
    /* Page is already loaded. */
    return true;
  }

  /* Obtain a frame to store the page. */
  void *kpage = frame_alloc (PAL_USER, p->addr);
  if (kpage == NULL) {
    return false;
  }
  frame_set_pinned (kpage, true);

  /* Load page data into the frame. */
  bool writable = true;

  switch (p->status)
  {
  case ALL_ZERO:
    /* Zeroed out page. */
    memset (kpage, 0, PGSIZE);
    break;

  case IN_FRAME:
    /* Page already loaded to frame, nothing more to do. */
    /* TODO: return false or true? */
    break;

  case SWAPPED:
    /* Swap in: load the data from the swap disc. */
    ASSERT ((int) p->swap_slot != -1);
    swap_in (kpage, p->swap_slot);
    add_to_pages (kpage, p);
    p->status = IN_FRAME;
    break;

  case FILE:
  case MMAPPED:
    /* Load page from file into allocated frame. */
    if (!file_share_page (p)) {
      return load_file_page (p, kpage);
    }
    load_page (pt, pagedir, p);
    return;

  default:
    ASSERT (false);
  }

  /* Point the page table entry for the faulting virtual address to the physical page. */
  if (!install_page (p->addr, kpage, writable)) {
     frame_free (kpage, true);
     return false;
  }

  /* Set SPTE data to point to the allocated kpage. */
  p->kpage = kpage;
  p->status = IN_FRAME;
  
  frame_set_pinned (kpage, false);
  pagedir_set_dirty (pagedir, kpage, false);

  return true;
}

bool file_share_page (struct page *p) {
  if (p->kpage != NULL) {
    return false;
  }

  // search page table for page pointing to same file 
  struct hash *pt = thread_current ()->page_table;
  struct page *temp = malloc (sizeof (struct page));
  struct hash_elem *e;
  temp->file = p->file;
  temp->offset = p->offset;
  e = hash_find (pt, &temp->hash_elem);
  free (temp);
  if (e == NULL) {
    /* No corresponding file in pt to share with */
    return false;
  }

  struct page *s = hash_entry (e, struct page, hash_elem);
  ASSERT (s != NULL);

  page_install_frame (pt, p->addr, s->kpage);

  add_to_pages (s->kpage, p);

  if (s->status == IN_FRAME) {
    return true;
  } else {
    p->status = s->status;
    printf ("SHARED PAGE IS NOT IN FRAME\n");
    return true;
  }
}
