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
#include "filesys/inode.h"
#include "filesys/file.h"

/* Lock to protect file system */
static struct lock filesys_lock;

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
  struct dir *dir; /* Need to record the opened dir, used for readdir */
  struct list_elem elem;
};

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
    case SYS_CHDIR:
      {
        /* Chdir contains 1 argument */
        get_args (f, args, 1);
        f->eax = syscall_chdir (*(const char **)args[0]);
        break;
      }
    case SYS_MKDIR:
      {
        /* Mkdir contains 1 argument */
        get_args (f, args, 1);
        f->eax = syscall_mkdir (*(const char **)args[0]);
        break;
      }
    case SYS_READDIR:
      {
        /* Readdir contains 2 arguments */
        get_args (f, args, 2);
        f->eax = syscall_readdir (*(int *)args[0], *(char **)args[1]);
        break;
      }
    case SYS_ISDIR:
      {
        /* Isdir contains 1 argument */
        get_args (f, args, 1);
        f->eax = syscall_isdir (*(int *)args[0]);
        break;
      }
    case SYS_INUMBER:
      {
        /* Inumber contains 1 argument */
        get_args (f, args, 1);
        f->eax = syscall_inumber (*(int *)args[0]);
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
  if (!ptr || !is_user_vaddr (ptr) || ptr < (void *)0x08048000
      || !pagedir_get_page (thread_current ()->pagedir, ptr))
    {
      syscall_exit (-1);
    }
}

/* Check whether the memeory range is valid between [start, start + size) */
static void
check_valid_mem (const void *start, size_t size)
{
  const char *ptr = start;
  /* The simplest way: just loop through and check the whole memory area */
  for (size_t i = 0; i < size; ++i)
    {
      check_valid_ptr ((const void *)ptr++);
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

static struct file_list_elem *
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
  bool success = filesys_create (file, initial_size, false);
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
  open_file->dir = NULL;
  lock_acquire (&filesys_lock);
  /* If the opened file is a dir, then need to record it */
  /* We need to use the dir in the readdir */
  if (file_get_inode (f)->data.is_dir)
    open_file->dir = dir_open (file_get_inode (open_file->file));
  lock_release (&filesys_lock);
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
  /* Write into a dir */
  if (syscall_isdir (fd))
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
  lock_acquire (&filesys_lock);
  file_close (f->file);
  if (f->dir)
    {
      /* Note here we just need to free the dir */
      /* Because the corresponding inode of the dir and file are the same */
      /* The inode is closed by file_close */
      /* We should not close the inode again */
      /* So, we cannot call dir_close */
      /* We just need to free the dir */
      /* to avoid memory leak */
      free (f->dir);
      f->dir = NULL;
    }
  lock_release (&filesys_lock);
  list_remove (&f->elem);
  /* Free to ensure no memory leak */
  free (f);
}

bool
syscall_chdir (const char *dir)
{
  check_valid_str (dir);
  lock_acquire (&filesys_lock);
  bool ret = filesys_chdir (dir);
  lock_release (&filesys_lock);
  return ret;
}

bool
syscall_mkdir (const char *dir)
{
  check_valid_str (dir);
  lock_acquire (&filesys_lock);
  /* Initially the size of the directory is 0 */
  bool ret = filesys_create (dir, 0, true);
  lock_release (&filesys_lock);
  return ret;
}

bool
syscall_readdir (int fd, char *name)
{
  check_valid_str (name);
  struct file_list_elem *f = get_file (fd);
  /* Use lock to protect filesystem */
  lock_acquire (&filesys_lock);
  /* Used the recorded dir */
  struct dir *dir = f->dir;
  bool ret = dir && dir_readdir (dir, name);
  lock_release (&filesys_lock);
  return ret;
}

bool
syscall_isdir (int fd)
{
  struct file_list_elem *f = get_file (fd);
  /* Use lock to protect filesystem */
  lock_acquire (&filesys_lock);
  struct inode *inode = file_get_inode (f->file);
  /* Check whether it is a dir */
  bool ret = inode && inode->data.is_dir;
  lock_release (&filesys_lock);
  return ret;
}

int
syscall_inumber (int fd)
{
  struct file_list_elem *f = get_file (fd);
  /* Use lock to protect filesystem */
  lock_acquire (&filesys_lock);
  int ret = inode_get_inumber (file_get_inode (f->file));
  lock_release (&filesys_lock);
  return ret;
}