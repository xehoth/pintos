#ifndef VM_FRAME_H
#define VM_FRAME_H
#include <debug.h>
#include <stdint.h>
#include <list.h>
#include "vm/page.h"
#include "threads/thread.h"

typedef struct frame_table_entry
{
  void *frame;                             /* Frame address (Physical) */
  tid_t owner;                             /* Owner thread */
  sup_page_table_entry_t *sup_table_entry; /* Corresponding sup table entry */
  struct list_elem elem;                   /* List elem used in frame table */
} frame_table_entry_t;

/* Allocate and initialize a frame table entry */
frame_table_entry_t *new_frame_table_entry (void *frame, tid_t owner,
                                            sup_page_table_entry_t *sup_entry);

/* Action condition function for frame_table_foreach_if */
typedef bool (*frame_table_action_cmp) (frame_table_entry_t *, void *);
/* Action function for frame_table_foreach_if */
/* Return true the for each will break immediately */
typedef bool (*frame_table_action_func) (frame_table_entry_t *);
/* For each element in frame table, do some actions in some conditions */
void frame_table_foreach_if (frame_table_action_cmp if_cmp, void *cmp_val,
                             frame_table_action_func action_func);
/* Compare entry->frame and page */
bool frame_table_entry_equal_page (frame_table_entry_t *entry, void *page);
/* Init frame table */
void frame_table_init (void);
/* Get a new page and maintain info in frame table */
void *frame_new_page (sup_page_table_entry_t *table_entry);
/* Free a page and update the frame table */
void frame_free_page (void *page);
/* evict a frame to swap space, and return that frame entry */
frame_table_entry_t *evict_one_frame (void);

bool frame_access_time_less (const struct list_elem *,
                             const struct list_elem *, void *aux UNUSED);

#endif