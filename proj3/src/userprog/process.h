#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

typedef int pid_t; /* Process id type: one to one mapping (tid) */

/* The status of process */
enum process_status
{
  PROCESS_RUNNING, /* Process is running */
  PROCESS_EXITED,  /* Process exited normally */
  PROCESS_INIT,    /* Process just init before running (like during loading) */
  PROCESS_ERROR,   /* Process encounter errors */
};

/* Process struct */
struct process
{
  pid_t pid;                  /* Process id */
  int exit_code;              /* Process exit code */
  struct semaphore load_sema; /* Sync to ensure loading is done */
  struct semaphore wait_sema; /* Sync to ensure father wait son */
  struct list_elem elem;      /* Used in process list */
  enum process_status status; /* Process status */
};

/* Init process infos that are maintained in thread */
/* Why split infos for process is to correctly handle zombie processes and */
/* To pass multi-oom easily and normally */
void process_thread_init (struct thread *th);
/* Create a process, here this create just create the process struct */
/* Not the general meaning of process */
struct process *process_create (struct thread *th);
/* Get the child process with given pid in the list l */
struct process *get_child_process (struct list *l, pid_t pid);

#endif /* userprog/process.h */