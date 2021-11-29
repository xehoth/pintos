#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdbool.h>

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
bool syscall_chdir (const char *dir);
bool syscall_mkdir (const char *dir);
bool syscall_readdir (int fd, char *name);
bool syscall_isdir (int fd);
int syscall_inumber (int fd);
#endif /* userprog/syscall.h */
