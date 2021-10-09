#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

struct lock filesys_lock;

struct list_file
{
  int fd;
  struct file *file;
  struct list_elem elem;
};

static void syscall_handler (struct intr_frame *);
static void check_valid_ptr (const void *ptr);
static void check_valid_mem (const void *start, size_t size);
static void check_valid_str (const char *str);
static void get_args (struct intr_frame *f, void *args[], int argc);

static void *checker_esp;

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f)
{
  check_valid_mem (f->esp, sizeof (void *));
  checker_esp = f->esp;
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
        get_args (f, args, 1);
        syscall_exit (*(int *)args[0]);
        break;
      }
    case SYS_EXEC:
      {
        get_args (f, args, 1);
        f->eax = syscall_exec (*(const char **)args[0]);
        break;
      }
    case SYS_WAIT:
      {
        get_args (f, args, 1);
        f->eax = syscall_wait (*(pid_t *)args[0]);
        break;
      }
    case SYS_CREATE:
      {
        get_args (f, args, 2);
        f->eax
            = syscall_create (*(const char **)args[0], *(unsigned *)args[1]);
        break;
      }
    case SYS_REMOVE:
      {
        get_args (f, args, 1);
        f->eax = syscall_remove (*(const char **)args[0]);
        break;
      }
    case SYS_OPEN:
      {
        get_args (f, args, 1);
        f->eax = syscall_open (*(const char **)args[0]);
        break;
      }
    case SYS_FILESIZE:
      {
        get_args (f, args, 1);
        f->eax = syscall_filesize (*(int *)args[0]);
        break;
      }
    case SYS_READ:
      {
        get_args (f, args, 3);
        f->eax = syscall_read (*(int *)args[0], *(void **)args[1],
                               *(unsigned *)args[2]);
        break;
      }
    case SYS_WRITE:
      {
        get_args (f, args, 3);
        f->eax = syscall_write (*(int *)args[0], *(const void **)args[1],
                                *(unsigned *)args[2]);
        break;
      }
    case SYS_SEEK:
      {
        get_args (f, args, 2);
        syscall_seek (*(int *)args[0], *(unsigned *)args[1]);
        break;
      }
    case SYS_TELL:
      {
        get_args (f, args, 1);
        f->eax = syscall_tell (*(int *)args[0]);
        break;
      }
    case SYS_CLOSE:
      {
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
        syscall_exit (-1);
        break;
      }
    }
}

static void
check_valid_ptr (const void *ptr)
{
  if (!ptr || !is_user_vaddr (ptr) || ptr < (void *)0x08048000)
    {
      syscall_exit (-1);
    }
  if (!pagedir_get_page (thread_current ()->pagedir, ptr))
    {
      if (!try_get_page (ptr, checker_esp))
        {
          syscall_exit (-1);
        }
    }
}

static void
check_valid_mem (const void *start, size_t size)
{
  void *last_page = NULL;
  for (size_t i = 0; i < size; ++i)
    {
      void *cur_page = pg_round_down (start + i);
      if (cur_page != last_page)
        check_valid_ptr (last_page = cur_page);
    }
}

static void
check_valid_str (const char *str)
{
  check_valid_ptr (str);
  while (*str)
    check_valid_ptr (++str);
}

static void
get_args (struct intr_frame *f, void *args[], int argc)
{
  for (int i = 0; i < argc; ++i)
    {
      void *ptr = ((char *)f->esp) + (i + 1) * 4;
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
  while (!list_empty (&cur->open_files))
    {
      struct list_file *f
          = list_entry (list_back (&cur->open_files), struct list_file, elem);
      syscall_close (f->fd);
    }
  cur->process->exit_code = status;
  thread_exit ();
}

pid_t
syscall_exec (const char *cmd_line)
{
  check_valid_str (cmd_line);
  lock_acquire (&filesys_lock);
  pid_t pid = process_execute (cmd_line);
  lock_release (&filesys_lock);
  if (pid == TID_ERROR)
    return -1;
  struct thread *child = get_thread (pid);
  if (!child)
    return -1;
  // note that when child thread exit
  // only child process alive
  struct process *child_process = child->process;
  // wait for child loading
  sema_down (&child_process->load_sema);
  // ensure load success
  // note that process can be exited
  bool success = child_process->status == PROCESS_RUNNING
                 || child_process->status == PROCESS_EXITED;
  if (!success)
    {
      // load failed
      // ensure child thread exited
      sema_down (&child_process->wait_sema);
      // clean up
      list_remove (&child_process->elem);
      free (child_process);
      return -1;
    }
  return pid;
}

int
syscall_wait (pid_t pid)
{
  return process_wait (pid);
}

bool
syscall_create (const char *file, unsigned initial_size)
{
  check_valid_str (file);
  lock_acquire (&filesys_lock);
  bool succeed = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return succeed;
}

bool
syscall_remove (const char *file)
{
  check_valid_str (file);
  lock_acquire (&filesys_lock);
  bool succeed = filesys_remove (file);
  lock_release (&filesys_lock);
  return succeed;
}

static struct list_file *
get_file (int fd)
{
  struct list *l = &thread_current ()->open_files;
  for (struct list_elem *e = list_begin (l); e != list_end (l);
       e = list_next (e))
    {
      struct list_file *f = list_entry (e, struct list_file, elem);
      if (f->fd == fd)
        return f;
    }
  // error: no file
  syscall_exit (-1);
  return NULL;
}

int
syscall_open (const char *file)
{
  check_valid_str (file);
  lock_acquire (&filesys_lock);
  struct file *f = filesys_open (file);
  lock_release (&filesys_lock);
  if (!f)
    return -1;
  struct thread *cur = thread_current ();

  struct list_file *open_file = malloc (sizeof (struct list_file));

  if (!open_file)
    return -1;
  open_file->fd = cur->fd++;
  open_file->file = f;
  list_push_back (&cur->open_files, &open_file->elem);
  return open_file->fd;
}

int
syscall_filesize (int fd)
{
  struct list_file *f = get_file (fd);
  lock_acquire (&filesys_lock);
  int len = file_length (f->file);
  lock_release (&filesys_lock);
  return len;
}

int
syscall_read (int fd, void *buffer, unsigned size)
{
  check_valid_mem (buffer, size);
  if (fd == STDIN_FILENO)
    {
      return input_getc ();
    }
  if (fd == STDOUT_FILENO)
    syscall_exit (-1);
  struct list_file *f = get_file (fd);
  lock_acquire (&filesys_lock);
  int len = file_read (f->file, buffer, size);
  lock_release (&filesys_lock);
  return len;
}

int
syscall_write (int fd, const void *buffer, unsigned size)
{
  check_valid_mem (buffer, size);
  if (fd == STDOUT_FILENO)
    {
      putbuf ((const char *)buffer, size);
      return size;
    }
  if (fd == STDIN_FILENO)
    syscall_exit (-1);
  struct list_file *f = get_file (fd);
  lock_acquire (&filesys_lock);
  int len = file_write (f->file, buffer, size);
  lock_release (&filesys_lock);
  return len;
}

void
syscall_seek (int fd, unsigned position)
{
  struct list_file *f = get_file (fd);
  lock_acquire (&filesys_lock);
  file_seek (f->file, position);
  lock_release (&filesys_lock);
}

unsigned
syscall_tell (int fd)
{
  struct list_file *f = get_file (fd);
  lock_acquire (&filesys_lock);
  int pos = file_tell (f->file);
  lock_release (&filesys_lock);
  return pos;
}

void
syscall_close (int fd)
{
  struct list_file *f = get_file (fd);
  struct thread *cur = thread_current ();
  bool need_lock = filesys_lock.holder != cur;
  if (need_lock)
    lock_acquire (&filesys_lock);
  file_close (f->file);
  if (need_lock)
    lock_release (&filesys_lock);
  list_remove (&f->elem);
  free (f);
}

static bool
mmap_check_input (int fd, void *addr)
{
  /* Not console and multiples of page size */
  return fd >= 2 && (uint32_t)addr % PGSIZE == 0;
}

static bool
mmap_reopen_file (int fd, struct file **f_ptr)
{
  struct list_file *f = get_file (fd);
  off_t file_size = file_length (f->file);
  // TODO:
  return false;
}

mapid_t
syscall_mmap (int fd, void *addr)
{
  check_valid_ptr (addr);
  /* Console or not multiples of page size */
  if (fd < 2 || (uint32_t)addr % PGSIZE)
    return -1;
  struct list_file *f_entry = get_file (fd);
  off_t file_size = 0;
  /* No file or has a length of zero bytes */
  if (!f_entry->file || !(file_size = file_length (f_entry->file)))
    return -1;
  struct file *f = NULL; /*do_file_reopen (f_entry->file);*/
  /* Failed to reopen */
  if (!f)
    return -1;
  // TODO:
}

void
syscall_munmap (mapid_t mapping)
{
  // TODO:
}