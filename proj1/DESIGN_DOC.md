# Project 1: Threads	Design Document

## Group

### Group INFO

Group Number: 03

Hongtu XU <xuht1@shanghaitech.edu.cn>

Zhanrui ZHANG <zhangzhr2@shanghaitech.edu.cn>

### Contribution

Hongtu Xu: 

- Structure design
- Priority scheduling

- Priority donation

Zhanrui Zhang:

- Alarm clock
- Advanced scheduling
- Design document

## Task 1 Alarm Clock

### Data Structure

> A1: Copy here the declaration of each new or changed struct or struct member, global or static variable, typedef, or enumeration.  Identify the purpose of each in 25 words or less.

```c
struct thread
{
  ...
  int64_t ticks_to_unblock;
  /* tick to unblock the thread */
  ...
};
```

### Algorithm

>A2: Briefly describe what happens in a call to `timer_sleep()`, including the effects of the timer interrupt handler.

When `timer_sleep ()` is called, we first check whether `TICKS` is a positive number. Then we calculate at which tick the thread should be unblocked by adding ticks to sleep to current tick number. And we record the "wake up" time in the member variable `ticks_to_unblock` of  `struct thread`.

The `timer_interrupt ()` will be invoked each tick, so we just need to check if any thread needs to be "waked up". So we loop through all threads by `thread_for_each ()` and check whether the current tick is equal to `ticks_to_unblock` of that thread.

> A3: What steps are taken to minimize the amount of time spent in the timer interrupt handler?

Check each thread's `tick_to_unblock`, this is the easiest way of implementation.

### Synchronization

> A4: How are race conditions avoided when multiple threads call `timer_sleep()` simultaneously?

Only the caller thread will be blocked.

> A5: How are race conditions avoided when a timer interrupt occurs during a call to `timer_sleep()`?

When we set `ticks_to_unblock`, we should use `intr_disable ()` to ensure that it can not be interrupted.

### Rationale

> A6: Why did you choose this design?  In what ways is it superior to another design you considered?

It does not take much CPU resources and is very easy to implement as well.



## Task 2 Priority Scheduling

### Data Structure

> B1: Copy here the declaration of each new or changed struct or struct member, global or static variable, typedef, or enumeration. Identify the purpose of each in 25 words or less.



```c
/* Treap struct */
struct treap
{
  struct treap_node *root; /* Root node of this treap */
  treap_cmp_func *cmp;     /* The comparing function used in this treap */
};

/* Treap note struct */
struct treap_node
{
  struct treap_node *child[2]; /* Tree struct childs, 0/1 for left and right */
  void *data;                  /* Data stored in treap node */
  uint32_t rank;               /* Treap node rank */
  int size;                    /* Size of the current node's subtree */
  struct treap *treap;         /* The root of this treap */
};
```



```c
struct thread
{
  ...
  struct treap_node node;   /* Treap element */
  int64_t ready_treap_fifo; /* Make cmp fifo when priority is equal */

  int base_priority;          /* The original priority without donation */
  struct treap holding_locks; /* Locks this thread hold */
  struct lock *waiting_lock;  /* The lock this thread is blocked from */
  ...
};
```



```c
/* Treap of processes in THREAD_READY state */
static struct treap ready_treap;
/* Ensure fifo property when priority is equal */
static uint64_t thread_ready_treap_fifo;
```



```c
struct semaphore
{
  ...
  struct treap waiters; /* Use treap as a priority queue */
};

/* Lock. */
struct lock
{
  ...
  struct treap_node node; /* Treap node for holding_locks */
  int max_priority;       /* Max priority among threads that are waiting */
};

struct condition
{
  /* struct list waiters; */  /* List of waiting threads. */
  struct semaphore semaphore; /* Just one semaphore is enough */
};
```



> B2: Explain the data structure used to track priority donation. Use ASCII art to diagram a nested donation.  (Alternately, submit a `.png `file.)

Add a waiting threads to represent the threads blocked by a lock in `struct lock`, and keep track of the max priority of the threads.

Record base priority in the `struct thread`, and add waiting_lock to record which lock the thread is blocked by.

### Algorithm

> B3: How do you ensure that the highest priority thread waiting for a lock, semaphore, or condition variable wakes up first?

Use treap to maintain ready threads. Each time the scheduler will take the thread with highest priority from treap.  Whenever the priority of certain thread is changed, yield that thread.

> B4: Describe the sequence of events when a call to `lock_acquire()` causes a priority donation. How is nested donation handled?

`lock_try_acquire()` will be called, and since now the lock is held by a thread with lower priority, it will return false. So `lock_acquire_fail()` will be invoked. Here we set the current thread's waiting lock to be this lock, and update priority through a chain, update the lock's max priority if necessary. At last, invoke `sema_down()`.

> B5: Describe the sequence of events when `lock_release()` is called on a lock that a higher-priority thread is waiting for.

First erase the lock from the holding lock treap of current thread. Then update the priority of current thread. Then we should set the holder of the lock to `NULL` and at last we call `sema_up()` to increase the semaphore value and unblock the waiting thread with highest priority. At last we yield current thread to ensure that higher-priority thread will run first.

### Synchronization

> B6: Describe a potential race in `thread_set_priority()` and explain how your implementation avoids it.  Can you use a lock to avoid this race?

When the thread itself wants to modify the priority, and there is another priority donation, there may be a potential race.   Interrupt is disabled and call `thread_yield()` to schedule.

A lock cannot avoid this race, we cannot share a lock between current thread and the lock.

### Rationale

> B7: Why did you choose this design?  In what ways is it superior to another design you considered?

Compared to implement priority queue based on a list, which takes at least O(n) time when putting a thread into ready queue, the treap is much more efficient, taking only O(log n) time.

## Task 3 Advanced Scheduler

### Data Structure

> C1: Copy here the declaration of each new or changed struct or struct member, global or static variable, typedef, or enumeration. Identify the purpose of each in 25 words or less.

```c
/* use int32_t as custom fix point type */
typedef int32_t fp32_t;

struct thread
{
  ...
  int nice; /* nice value of the thread, -20 to 20 */
  fp32_t recent_cpu; /* recent cpu time, fixed point */
  ...
};

static fp32_t load_avg; /* global varible representing load average */
```

### Algorithm

>C2: Suppose threads A, B, and C have nice values 0, 1, and 2.  Each has a `recent_cpu` value of 0.  Fill in the table below showing the scheduling decision and the `priority` and `recent_cpu` values for each thread after each given number of timer ticks:

| timer ticks | recent_cpu  A | recent_cpu B | recent_cpu C | priority A | priority B | priority C | Thread To Run |
| :---------: | :-----------: | :----------: | :----------: | :--------: | :--------: | :--------: | :-----------: |
|      0      |       0       |      0       |      0       |     63     |     61     |     59     |       A       |
|      4      |       4       |      0       |      0       |     62     |     61     |     59     |       A       |
|      8      |       8       |      0       |      0       |     61     |     61     |     59     |       B       |
|     12      |       8       |      4       |      0       |     61     |     60     |     59     |       A       |
|     16      |      12       |      4       |      0       |     60     |     60     |     59     |       B       |
|     20      |      12       |      8       |      0       |     60     |     59     |     59     |       A       |
|     24      |      16       |      8       |      0       |     59     |     59     |     59     |       C       |
|     28      |      16       |      8       |      4       |     59     |     59     |     58     |       B       |
|     32      |      16       |      12      |      4       |     59     |     58     |     58     |       A       |
|     36      |      20       |      12      |      4       |     58     |     58     |     58     |       C       |



> C3: Did any ambiguities in the scheduler specification make values in the table uncertain?  If so, what rule did you use to resolve them?  Does this match the behavior of your scheduler?

When different threads have same priority, it is not specified which thread to run. In the table, we follow FIFO rule.

> C4: How is the way you divided the cost of scheduling between code inside and outside interrupt context likely to affect performance?

Updating `recent_cpu` and `load_avg` of the system is done in interrupt context, if this part takes too much time, the system performance will be influenced.

### Rationale

> C5: Briefly critique your design, pointing out advantages and disadvantages in your design choices.  If you were to have extra time to work on this part of the project, how might you choose to refine or improve your design?

Our design is strictly following the requirement and there isn't much to improve if we just need to meet the specification. In real application, may we can try to get some better value for those arguments when calculating the priority of certain threads.

> C6: The assignment explains arithmetic for fixed-point math in detail, but it leaves it open to you to implement it.  Why did you decide to implement it the way you did? If you created an abstraction layer for fixed-point math, that is, an abstract data type and/or a set of functions or macros to manipulate fixed-point numbers, why did you do so?  If not, why not?

In `fixed_point.h`,several functions is defined to perform fix point math to perform certain calculation between fix point number and integer, which will be used in task 3. Defining new functions provides clear abstraction while it is also very friendly when implementing math. And there won't be much performance lose compared to defining macros.