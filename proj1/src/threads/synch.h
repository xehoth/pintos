#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include "threads/treap.h"
#include <list.h>
#include <stdbool.h>

/* A counting semaphore. */
struct semaphore
{
  unsigned value; /* Current value. */
  /*struct list waiters;*/
  /* List of waiting threads. */
  struct treap waiters; /* Use treap as a priority queue */
};

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* Lock. */
struct lock
{
  struct thread *holder;      /* Thread holding lock (for debugging). */
  struct semaphore semaphore; /* Binary semaphore controlling access. */

  struct treap_node node; /* Treap node for holding_locks */
  int max_priority;       /* Max priority among threads that are waiting */
};

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable. */
struct condition
{
  /* struct list waiters; */  /* List of waiting threads. */
  struct semaphore semaphore; /* Just one semaphore is enough */
};

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

/* Optimization barrier.

   The compiler will not reorder operations across an
   optimization barrier.  See "Optimization Barriers" in the
   reference guide for more information.*/
#define barrier() asm volatile("" : : : "memory")

/* When success, apply lock hold event */
void lock_acquire_success (struct lock *);
/* When failed, this thread will be blocked */
/* Do donation to the lock */
void lock_acquire_fail (struct lock *);
/* Treap cmp func used in lock waiters treap */
bool lock_priority_threap_cmp (const struct treap_node *a,
                               const struct treap_node *b);
#endif /* threads/synch.h */
