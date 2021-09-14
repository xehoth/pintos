#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
// !BEGIN MODIFY
#include "threads/synch.h"
// !END MODIFY

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

// !BEGIN MODIFY
typedef int pid_t;

enum process_status
{
  PROCESS_RUNNING,
  PROCESS_EXITED,
  PROCESS_INIT,
  PROCESS_ERROR,
};

struct process
{
  pid_t pid;
  int exit_code;
  struct semaphore load_sema;
  struct semaphore wait_sema;
  struct list_elem elem;
  enum process_status status;
};

void process_thread_init (struct thread *th);
struct process *process_create (struct thread *th);
struct process *get_child_process (struct list *l, pid_t pid);
// !END MODIFY
#endif /* userprog/process.h */
