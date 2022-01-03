#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "vm/frame.h"
#include "vm/page.h"

/* Lock to protect file system */
struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);
/* Check whether the given ptr is valid */
static void check_valid_ptr (const void *ptr);
/* Check whether the memeory range is valid between [start, start + size) */
static void check_valid_mem (const void *start, size_t size);
/* Check whether the given string is valid */
static void check_valid_str (const char *str);
/* Get [argc] args from [f->esp] to [args] with memory checking */
static void get_args (struct intr_frame *f, void *args[], int argc);

struct file_list_elem
{
  int fd; /* File descriptor */
  struct file *file;
  struct list_elem elem;
};

static void *checker_esp;

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  /* Init filesystem lock */
  lock_init (&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f)
{
  /* First need to check whether the address of stack is valid */
  check_valid_mem (f->esp, sizeof (void *));
  checker_esp = f->esp;
  /* The max number of args is 3 */
  void *args[3];
  switch (*(int *)f->esp)
    {
    case SYS_HALT:
      {
        syscall_halt ();
        break;
      }
    case SYS_EXIT:
      {
        /* Exit contains 1 argument */
        get_args (f, args, 1);
        syscall_exit (*(int *)args[0]);
        break;
      }
    case SYS_EXEC:
      {
        /* Exec contains 1 argument */
        get_args (f, args, 1);
        f->eax = syscall_exec (*(const char **)args[0]);
        break;
      }
    case SYS_WAIT:
      {
        /* Wait contains 1 argument */
        get_args (f, args, 1);
        f->eax = syscall_wait (*(pid_t *)args[0]);
        break;
      }
    case SYS_CREATE:
      {
        /* Create contains 2 arguments */
        get_args (f, args, 2);
        f->eax
            = syscall_create (*(const char **)args[0], *(unsigned *)args[1]);
        break;
      }
    case SYS_REMOVE:
      {
        /* Remove contains 1 argument */
        get_args (f, args, 1);
        f->eax = syscall_remove (*(const char **)args[0]);
        break;
      }
    case SYS_OPEN:
      {
        /* Open contains 1 argument */
        get_args (f, args, 1);
        f->eax = syscall_open (*(const char **)args[0]);
        break;
      }
    case SYS_FILESIZE:
      {
        /* Filesize contains 1 argument */
        get_args (f, args, 1);
        f->eax = syscall_filesize (*(int *)args[0]);
        break;
      }
    case SYS_READ:
      {
        /* Read contains 3 arguments */
        get_args (f, args, 3);
        f->eax = syscall_read (*(int *)args[0], *(void **)args[1],
                               *(unsigned *)args[2]);
        break;
      }
    case SYS_WRITE:
      {
        /* Write contains 3 arguments */
        get_args (f, args, 3);
        f->eax = syscall_write (*(int *)args[0], *(const void **)args[1],
                                *(unsigned *)args[2]);
        break;
      }
    case SYS_SEEK:
      {
        /* Seek contains 2 arguments */
        get_args (f, args, 2);
        syscall_seek (*(int *)args[0], *(unsigned *)args[1]);
        break;
      }
    case SYS_TELL:
      {
        /* Tell contains 1 argument */
        get_args (f, args, 1);
        f->eax = syscall_tell (*(int *)args[0]);
        break;
      }
    case SYS_CLOSE:
      {
        /* Close contains 1 argument */
        get_args (f, args, 1);
        syscall_close (*(int *)args[0]);
        break;
      }
    case SYS_MMAP:
      {
        get_args (f, args, 2);
        f->eax = syscall_mmap (*(int *)args[0], *(void **)args[1]);
        break;
      }
    case SYS_MUNMAP:
      {
        get_args (f, args, 1);
        syscall_munmap (*(mapid_t *)args[0]);
        break;
      }
    default:
      {
        /* No matching then exit(-1) */
        syscall_exit (-1);
        break;
      }
    }
}

/* Check whether the given ptr is valid */
static void
check_valid_ptr (const void *ptr)
{
  /* [ptr] should not be null, should be in user addr and in page */
  if (!ptr || !is_user_vaddr (ptr) || ptr < (void *)0x08048000)
    {
      syscall_exit (-1);
    }
  if (!pagedir_get_page (thread_current ()->pagedir, ptr))
    {
      if (!try_get_page ((void *)ptr, checker_esp))
        syscall_exit (-1);
    }
}

/* Check whether the memeory range is valid between [start, start + size) */
static void
check_valid_mem (const void *start, size_t size)
{
  void *last_page = NULL;
  /* The simplest way: just loop through and check the whole memory area */
  for (size_t i = 0; i < size; ++i)
    {
      void *cur_page = pg_round_down (start + i);
      if (cur_page != last_page)
        check_valid_ptr (last_page = cur_page);
    }
}

/* Check whether the given string is valid */
static void
check_valid_str (const char *str)
{
  /* First check the start addr */
  check_valid_ptr (str);
  /* Then check the whole addr of the string */
  while (*str)
    check_valid_ptr (++str);
}

/* Get [argc] args from [f->esp] to [args] with memory checking */
static void
get_args (struct intr_frame *f, void *args[], int argc)
{
  for (int i = 0; i < argc; ++i)
    {
      /* 4 byte for each argument */
      void *ptr = ((char *)f->esp) + (i + 1) * 4;
      /* With memory checking */
      check_valid_mem (ptr, sizeof (void *));
      args[i] = ptr;
    }
}

void
syscall_halt ()
{
  shutdown_power_off ();
}

void
syscall_exit (int status)
{
  struct thread *cur = thread_current ();
  /* If there are open files, we should close them first */
  while (!list_empty (&cur->open_files))
    {
      struct file_list_elem *f = list_entry (list_back (&cur->open_files),
                                             struct file_list_elem, elem);
      syscall_close (f->fd);
    }
  cur->process->exit_code = status;
  thread_exit ();
}

pid_t
syscall_exec (const char *cmd_line)
{
  check_valid_str (cmd_line);
  /* Since start_process in execute will invoke load from file */
  /* Then need to protect the filesystem */
  lock_acquire (&filesys_lock);
  pid_t pid = process_execute (cmd_line);
  lock_release (&filesys_lock);
  /* -1 for error */
  if (pid == TID_ERROR)
    return -1;
  struct thread *child = get_thread (pid);
  /* -1 for error */
  if (!child)
    return -1;
  /* Note that when child thread exit */
  /* Only child process (the information struct) alive */
  struct process *child_process = child->process;
  /* Wait for child loading */
  sema_down (&child_process->load_sema);
  /* Ensure load success */
  /* Note that process can be exited */
  bool success = child_process->status == PROCESS_RUNNING
                 || child_process->status == PROCESS_EXITED;
  if (!success)
    {
      /* Load failed */
      /* Ensure child thread exited */
      sema_down (&child_process->wait_sema);
      /* Clean up */
      list_remove (&child_process->elem);
      free (child_process);
      /* -1 for error */
      return -1;
    }
  return pid;
}

int
syscall_wait (pid_t pid)
{
  /* Just invoke process_wait */
  return process_wait (pid);
}

struct file_list_elem *
get_file (int fd)
{
  struct list *open_file_list = &thread_current ()->open_files;
  /* Loop through current thread's files, and find fd */
  for (struct list_elem *e = list_begin (open_file_list);
       e != list_end (open_file_list); e = list_next (e))
    {
      struct file_list_elem *f = list_entry (e, struct file_list_elem, elem);
      if (f->fd == fd)
        return f;
    }
  /* When fd not found */
  syscall_exit (-1);
  return NULL;
}

bool
syscall_create (const char *file, unsigned initial_size)
{
  /* Check if file name is valid */
  check_valid_str (file);
  /* Use lock to protect filesystem */
  lock_acquire (&filesys_lock);
  /* Invoke func provided in filesys */
  bool success = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return success;
}

bool
syscall_remove (const char *file)
{
  /* Check if file name is valid */
  check_valid_str (file);
  /* Use lock to protect filesystem */
  lock_acquire (&filesys_lock);
  /* Invoke func provided in filesys */
  bool success = filesys_remove (file);
  lock_release (&filesys_lock);
  return success;
}

int
syscall_open (const char *file)
{
  check_valid_str (file);
  /* Use lock to protect filesystem */
  lock_acquire (&filesys_lock);
  struct file *f = filesys_open (file);
  lock_release (&filesys_lock);
  /* Return if file open failed */
  if (!f)
    return -1;
  struct thread *cur = thread_current ();
  struct file_list_elem *open_file = malloc (sizeof (struct file_list_elem));
  /* If open failed */
  if (!open_file)
    return -1;
  /* Initialize open file list entry */
  open_file->fd = cur->fd++;
  open_file->file = f;
  /* Add this file to the open file list of current thread */
  list_push_back (&cur->open_files, &open_file->elem);
  return open_file->fd;
}

int
syscall_filesize (int fd)
{
  struct file_list_elem *f = get_file (fd);
  /* Use lock to protect filesystem */
  lock_acquire (&filesys_lock);
  int len = file_length (f->file);
  lock_release (&filesys_lock);
  return len;
}

int
syscall_read (int fd, void *buffer, unsigned size)
{
  /* Target memory addr should be valid */
  check_valid_mem (buffer, size);
  if (fd == STDIN_FILENO)
    return input_getc ();
  /* Cannot read from std out */
  if (fd == STDOUT_FILENO)
    syscall_exit (-1);
  struct file_list_elem *f = get_file (fd);
  /* Use lock to protect filesystem */
  lock_acquire (&filesys_lock);
  /* Read file */
  int len = file_read (f->file, buffer, size);
  lock_release (&filesys_lock);
  return len;
}

int
syscall_write (int fd, const void *buffer, unsigned size)
{
  /* Source mem addr should be valid */
  check_valid_mem (buffer, size);
  if (fd == STDOUT_FILENO)
    {
      putbuf ((const char *)buffer, size);
      return size;
    }
  /* Cannot write to std in */
  if (fd == STDIN_FILENO)
    syscall_exit (-1);

  struct file_list_elem *f = get_file (fd);
  /* Use lock to protect filesystem */
  lock_acquire (&filesys_lock);
  /* Write file */
  int len = file_write (f->file, buffer, size);
  lock_release (&filesys_lock);
  return len;
}

void
syscall_seek (int fd, unsigned position)
{
  struct file_list_elem *f = get_file (fd);
  /* Use lock to protect filesystem */
  lock_acquire (&filesys_lock);
  file_seek (f->file, position);
  lock_release (&filesys_lock);
}

unsigned
syscall_tell (int fd)
{
  struct file_list_elem *f = get_file (fd);
  /* Use lock to protect filesystem */
  lock_acquire (&filesys_lock);
  int pos = file_tell (f->file);
  lock_release (&filesys_lock);
  return pos;
}

void
syscall_close (int fd)
{
  struct file_list_elem *f = get_file (fd);
  /* Use lock to protect filesystem */
  struct thread *cur = thread_current ();
  bool need_lock = (filesys_lock.holder != cur);
  if (need_lock)
    lock_acquire (&filesys_lock);
  file_close (f->file);
  if (need_lock)
    lock_release (&filesys_lock);
  list_remove (&f->elem);
  /* Free to ensure no memory leak */
  free (f);
}

static mmap_entry_t *
new_mmap_entry (void *addr, struct file *file, int page_count)
{
  /* Allocate space for new mmap entry */
  mmap_entry_t *entry = (mmap_entry_t *)malloc (sizeof (mmap_entry_t));
  if (!entry)
    return NULL;
  /* Initialize the entry if malloc success */
  entry->id = thread_current ()->mmap_id++;
  entry->addr = addr;
  entry->file = file;
  entry->page_count = page_count;
  return entry;
}

/* Release resources for mmap */
static void
do_free_mmap_entry (mmap_entry_t *entry)
{
  struct thread *cur = thread_current ();
  void *addr = entry->addr;
  /* Iterate through all mapped page and release resources */
  for (int cur_page = 0; cur_page < entry->page_count; ++cur_page)
    {
      sup_page_table_entry_t *table_entry
          = sup_table_find (&cur->sup_page_table, addr);
      if (table_entry)
        {
          /* If dirty: write back */
          if (pagedir_is_dirty (cur->pagedir, addr))
            {
              lock_acquire (&filesys_lock);
              file_seek (table_entry->file, table_entry->ofs);
              file_write (table_entry->file, addr, table_entry->read_bytes);
              lock_release (&filesys_lock);
            }
          /* Delete from page table */
          if (pagedir_get_page (cur->pagedir, table_entry->addr))
            {
              frame_free_page (
                  pagedir_get_page (cur->pagedir, table_entry->addr));
              pagedir_clear_page (cur->pagedir, table_entry->addr);
            }
          /* Clear sup page table */
          hash_delete (&cur->sup_page_table, &table_entry->hash_elem);
        }
      addr += PGSIZE;
    }

  lock_acquire (&filesys_lock);
  file_close (entry->file);
  lock_release (&filesys_lock);
  free (entry);
}

static bool
check_mmap_overlaps (void *addr, int size)
{
  if (!addr || size < 0)
    return false;
  struct thread *cur = thread_current ();
  /* Iterate over address one file takes and check each sup page table and page
   * table */
  for (; size >= 0; size -= PGSIZE)
    {
      if (sup_table_find (&cur->sup_page_table, addr)
          || pagedir_get_page (cur->pagedir, addr))
        return false;
      addr += PGSIZE;
    }
  return true;
}

mapid_t
syscall_mmap (int fd, void *addr)
{
  /* Console or not multiples of page size */
  if (fd < 2 || (uint32_t)addr % PGSIZE)
    return -1;
  struct file_list_elem *f_entry = get_file (fd);
  off_t file_size = 0;
  /* No file or has a length of zero bytes */
  if (!f_entry->file || !(file_size = file_length (f_entry->file)))
    return -1;
  lock_acquire (&filesys_lock);
  struct file *f = file_reopen (f_entry->file);
  lock_release (&filesys_lock);
  /* Failed to reopen */
  if (!f)
    return -1;
  /* Check if addr has overlaps */
  if (!check_mmap_overlaps (addr, file_size))
    {
      return -1;
    }
  uint32_t read_bytes = file_size;
  uint32_t zero_bytes = (PGSIZE - read_bytes % PGSIZE) % PGSIZE;
  int page_count = (read_bytes + zero_bytes) / PGSIZE;
  mmap_entry_t *mmap_entry = new_mmap_entry (addr, f, page_count);
  /* Lazy load the file from disk */
  if (!lazy_load (f, 0, addr, read_bytes, zero_bytes, true, true))
    {
      free (mmap_entry);
      return -1;
    }
  /* Add mmap entry into mmap list */
  list_push_back (&thread_current ()->mmap_list, &mmap_entry->elem);
  return mmap_entry->id;
}

void
syscall_munmap (mapid_t mapping)
{
  struct thread *cur = thread_current ();
  /* Iterate over the mmap list and unmap all corresponding entry */
  for (struct list_elem *e = list_begin (&cur->mmap_list);
       e != list_end (&cur->mmap_list); e = list_next (e))
    {
      mmap_entry_t *entry = list_entry (e, mmap_entry_t, elem);
      if (entry->id == mapping)
        {
          list_remove (e);
          do_free_mmap_entry (entry);
          return;
        }
    }
}