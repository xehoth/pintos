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
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"

/* Return address type */
typedef void (*ret_addr_t) (void);

/* The stack types: union is more elegant */
typedef union
{
  char *p_char;     /* Use to modify char type */
  char **p_pchar;   /* Use to modify char[]/char* type */
  char ***p_ppchar; /* Use to modify char** type */
  uint8_t *p_u8;    /* Use to modify uint8_t type */
  unsigned u32;     /* Use to get the address value */
  int *p_int;       /* Use to modify int type */
  ret_addr_t *p_ra; /* Use to modify return address */
} esp_t;

/* Parse arguments for the stack */
static void parse_args (esp_t *esp, char *args_str, char *save_ptr);
/* Deny write to self file */
static bool deny_write_to_self (struct thread *cur, const char *name);
/* Recover the deny for writing to self */
static void recover_write_to_self (struct thread *cur);

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name)
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  /* Another copy of FILE_NAME, but only the first token (only file name) */
  char *exact_file_name = palloc_get_page (0);
  /* Failed to get a page */
  if (!exact_file_name)
    {
      /* Prevent memory leak */
      palloc_free_page (fn_copy);
      return TID_ERROR;
    }
  /* Make copy of file_name */
  strlcpy (exact_file_name, file_name, PGSIZE);
  strlcpy (fn_copy, file_name, PGSIZE);

  /* split with " " */
  char *save_ptr;
  char *token = strtok_r (exact_file_name, " ", &save_ptr);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (token, PRI_DEFAULT, start_process, fn_copy);
  /* Prevent memory leak */
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy);

  /* Free the allocated page */
  palloc_free_page (exact_file_name);

  /* Thread create success */
  if (tid != TID_ERROR)
    {
      /* Get the child process */
      struct thread *child = get_thread (tid);
      ASSERT (child);
      ASSERT (child->parent);
      /* Insert child into parent's list */
      list_push_back (&child->parent->child_list, &child->process->elem);
    }
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  /* Split with " ", get the file name */
  char *save_ptr;
  char *token = strtok_r (file_name, " ", &save_ptr);

  success = load (file_name, &if_.eip, &if_.esp);

  struct thread *cur = thread_current ();
  /* If load failed, quit. */
  if (!success)
    {
      /* Prevent memory leak */
      palloc_free_page (file_name);
      /* Set the runnin status to error because of loading failed */
      cur->process->status = PROCESS_ERROR;
      /* Load has been finished, sema up the load sema */
      sema_up (&cur->process->load_sema);
      /* Quit */
      thread_exit ();
    }
  else
    {
      /* Load success, then parse arguments to stack */
      parse_args ((esp_t *)&if_.esp, token, save_ptr);
      /* Deny writing to self */
      bool deny_success = deny_write_to_self (cur, token);
      /* Remember to free the allocated page, prevent memory leak */
      palloc_free_page (file_name);
      if (deny_success)
        {
          /* Set status to running normally */
          cur->process->status = PROCESS_RUNNING;
          /* Load has been finished, sema up the load sema */
          sema_up (&cur->process->load_sema);
        }
      else
        {
          /* Failed to deny writing to self */
          /* Set the running status to error */
          cur->process->status = PROCESS_ERROR;
          /* Load has been finished, sema up the load sema */
          sema_up (&cur->process->load_sema);
          /* Quit */
          thread_exit ();
        }
    }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid)
{
  struct thread *cur = thread_current ();
  /* Get the child according to the pid */
  struct process *child = get_child_process (&cur->child_list, child_tid);
  /* Not found, then error */
  if (!child)
    return -1;
  /* Wait for child running */
  sema_down (&child->wait_sema);
  /* Child run finish */
  /* Then remove from child list */
  list_remove (&child->elem);
  /* The process struct is allocated by malloc */
  /* So, even if child thread has been destroyed */
  /* We can still get the information in the child struct */
  /* Get the child process's exit code */
  int exit_code = child->exit_code;
  /* Need to free to prevent memory leak */
  free (child);
  return exit_code;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      /* Print the exit code */
      printf ("%s: exit(%d)\n", cur->name, cur->process->exit_code);
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

  /* Change the a normally running process's status to exited  */
  if (cur->process->status != PROCESS_ERROR)
    cur->process->status = PROCESS_EXITED;
  /* Recover write */
  recover_write_to_self (cur);
  /* Process exit, then sema up the wait sema */
  sema_up (&cur->process->wait_sema);
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
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
{
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
{
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", file_name);
      goto done;
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2
      || ehdr.e_machine != 3 || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr) || ehdr.e_phnum > 1024)
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
              if (!load_segment (file, file_page, (void *)mem_page, read_bytes,
                                 zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void))ehdr.e_entry;

  success = true;

done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
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
  if (phdr->p_offset > (Elf32_Off)file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *)phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *)(phdr->p_vaddr + phdr->p_memsz)))
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

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int)page_read_bytes)
        {
          palloc_free_page (kpage);
          return false;
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable))
        {
          palloc_free_page (kpage);
          return false;
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp)
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL)
    {
      success = install_page (((uint8_t *)PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
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

/* Parse arguments for the stack */
static void
parse_args (esp_t *esp, char *args_str, char *save_ptr)
{
  /* Start from save_ptr, the first token is process it self */
  int argc = 1;
  /* First get the argument num */
  for (char *c = save_ptr;;)
    {
      /* Skip the space before an argument */
      while (c && *c && *c == ' ')
        ++c;
      if (!c || !*c)
        break;
      /* One valid argument */
      ++argc;
      /* Move to next token */
      while (c && *c && *c != ' ')
        ++c;
    }
  /* We can use this dynamic array because we are using C */
  /* This is currently supported in C standard but not in C++ */
  char *argv[argc];
  int argv_len = 0;
  /* Push args */
  for (char *arg = args_str; arg; arg = strtok_r (NULL, " ", &save_ptr))
    {
      argv[argv_len++] = arg;
    }
  ASSERT (argv_len == argc);

  /* Store the arg adress according to the table in Stanford web page */
  char *args_addr[argc];
  /* Arguments need to be pushed reversely, since stack grow downward */
  for (int i = argc - 1; i >= 0; --i)
    {
      const int arg_len = strlen (argv[i]);
      /* Add 1 for '\0' */
      esp->p_char -= arg_len + 1;
      /* Copy arguments to the stack */
      strlcpy (esp->p_char, argv[i], arg_len + 1);
      /* Store the address for argv[i] in the stack */
      args_addr[i] = esp->p_char;
    }

  /* Word align */
  while (esp->u32 % 4 != 0)
    *--esp->p_u8 = 0;

  /* Push args addr */
  /* Last empty addr */
  *--esp->p_pchar = NULL;

  /* Push args addr */
  for (int i = argc - 1; i >= 0; --i)
    {
      *--esp->p_pchar = args_addr[i];
    }

  /* Push argv */
  char **argv_start = esp->p_pchar;
  *--esp->p_ppchar = argv_start;

  /* Push argc */
  *--esp->p_int = argc;

  /* Push return address */
  *--esp->p_ra = NULL;
}

/* Deny write to self file */
static bool
deny_write_to_self (struct thread *cur, const char *name)
{
  /* Open self file */
  cur->self_file = filesys_open (name);
  /* Open failed */
  if (!cur->self_file)
    return false;
  /* Deny write */
  file_deny_write (cur->self_file);
  return true;
}

/* Recover the deny for writing to self */
static void
recover_write_to_self (struct thread *cur)
{
  /* First ensure the self file exists */
  if (cur->self_file)
    {
      /* Recover: allow write */
      file_allow_write (cur->self_file);
      /* Close the file */
      file_close (cur->self_file);
      cur->self_file = NULL;
    }
}

/* Init process infos that are maintained in thread */
void
process_thread_init (struct thread *th)
{
  th->parent = NULL;
  th->process = NULL;
  list_init (&th->child_list);
  list_init (&th->open_files);
  th->fd = 2; /* Reserved for stdin and stdout */
  th->self_file = NULL;
}

/* Create a process, here this create just create the process struct */
/* Not the general meaning of process */
struct process *
process_create (struct thread *th)
{
  struct process *proc = malloc (sizeof (struct process));
  if (!proc)
    {
      return NULL;
    }
  proc->pid = th->tid;
  proc->exit_code = -1;
  proc->status = PROCESS_INIT;
  sema_init (&proc->wait_sema, 0);
  sema_init (&proc->load_sema, 0);
  return proc;
}

/* Get the child process with given pid in the list l */
struct process *
get_child_process (struct list *l, pid_t pid)
{
  struct list_elem *e;
  /* Loop through list l */
  for (e = list_begin (l); e != list_end (l); e = list_next (e))
    {
      struct process *proc = list_entry (e, struct process, elem);
      /* Check whether the current proc's pid == the given pid */
      if (proc->pid == pid)
        return proc;
    }
  /* Not found */
  return NULL;
}