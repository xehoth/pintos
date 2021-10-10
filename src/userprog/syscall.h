#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdbool.h>
#include <list.h>

typedef int mapid_t;

typedef struct mmap_entry
{
  mapid_t id;
  void *addr; /* User virtual addr */
  struct file *file;
  int page_count; /* Number of pages */
  struct list_elem elem;
} mmap_entry_t;

void syscall_init (void);

void syscall_halt (void);
void syscall_exit (int status);
int syscall_exec (const char *cmd_line);
int syscall_wait (int pid);
bool syscall_create (const char *file, unsigned initial_size);
bool syscall_remove (const char *file);
int syscall_open (const char *file);
int syscall_filesize (int fd);
int syscall_read (int fd, void *buffer, unsigned size);
int syscall_write (int fd, const void *buffer, unsigned size);
void syscall_seek (int fd, unsigned position);
unsigned syscall_tell (int fd);
void syscall_close (int fd);

mapid_t syscall_mmap (int fd, void *addr);
void syscall_munmap (mapid_t mapping);
#endif /* userprog/syscall.h */