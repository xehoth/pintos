#ifndef VM_FRAME_H
#define VM_FRAME_H
#include <debug.h>
#include <stdint.h>
#include <list.h>
#include "vm/page.h"
#include "threads/thread.h"

typedef struct frame_table_entry
{
  void *frame_addr;                        /* Address of frame */
  tid_t owner;                             /* Owner of the frame */
  sup_page_table_entry_t *sup_table_entry; /* Corresponding sup table entry */
  struct list_elem elem;                   /* List elem in frame table */
} frame_table_entry_t;

/* Allocate and initialize a frame table entry */
frame_table_entry_t *new_frame_table_entry (void *frame_addr, tid_t owner,
                                            sup_page_table_entry_t *sup_entry);

/* Get a new frame and maintain info in frame table */
frame_table_entry_t *frame_new_page (sup_page_table_entry_t *sup_entry);

void frame_free_page (void *frame_addr);

/* Action condition function for frame_table_foreach_if */
typedef bool (*frame_table_action_cmp) (frame_table_entry_t *, void *);
/* Action function for frame_table_foreach_if */
/* Return true the for each will break immediately */
typedef bool (*frame_table_action_func) (frame_table_entry_t *);
/* For each element in frame table, do some actions in some conditions */
void frame_table_foreach_if (frame_table_action_cmp if_cmp, void *cmp_val,
                             frame_table_action_func action_func);
/* Compare entry->frame and page */
bool frame_table_entry_crspd_frame (frame_table_entry_t *entry,
                                    void *frame_addr);
/* Init frame table */
void frame_table_init (void);
/* Evict a frame to swap space and return that frame entry */
frame_table_entry_t *evict_one_frame (void);

bool frame_access_time_less (const struct list_elem *,
                             const struct list_elem *, void *aux UNUSED);
#endif