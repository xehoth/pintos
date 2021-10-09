#include "vm/frame.h"
#include "vm/swap.h"
#include "userprog/pagedir.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* Frame table list, store frame table entries */
static struct list frame_table;
/* Avoid data race for frame table operations */
static struct lock frame_table_lock;

/* Allocate and initialize a frame table entry */
frame_table_entry_t *
new_frame_table_entry (void *frame, tid_t owner,
                       sup_page_table_entry_t *sup_entry)
{
  frame_table_entry_t *entry
      = (frame_table_entry_t *)malloc (sizeof (frame_table_entry_t));
  /* Malloc failed */
  if (!entry)
    return NULL;
  entry->frame = frame;
  entry->owner = owner;
  entry->sup_table_entry = sup_entry;
  return entry;
}

/* For each element in frame table, do some actions in some conditions */
void
frame_table_foreach_if (frame_table_action_cmp if_cmp, void *cmp_val,
                        frame_table_action_func action_func)
{
  lock_acquire (&frame_table_lock);
  for (struct list_elem *e = list_begin (&frame_table), *next;
       e != list_end (&frame_table); e = next)
    {
      next = list_next (e);
      frame_table_entry_t *entry = list_entry (e, frame_table_entry_t, elem);
      if (if_cmp (entry, cmp_val) && action_func (entry))
        {
          break;
        }
    }
  lock_release (&frame_table_lock);
}

/* Compare entry->frame and page */
bool
frame_table_entry_equal_page (frame_table_entry_t *entry, void *page)
{
  return entry->frame == page;
}

/* Init frame table */
void
frame_table_init ()
{
  list_init (&frame_table);
  lock_init (&frame_table_lock);
}

/* Get a new page and maintain info in frame table */
frame_table_entry_t *
frame_new_page (sup_page_table_entry_t *table_entry)
{
  /* Invalid */
  if (!table_entry)
    return NULL;
  void *kernal_page = palloc_get_page (PAL_USER);
  frame_table_entry_t *frame_entry;
  if (!kernal_page)
    {
      /* evict one frame and reuse this frame table entry */
      frame_entry = evict_one_frame ();
      /* set tid and sup table entry */
      frame_entry->owner = thread_tid ();
      frame_entry->sup_table_entry = table_entry;
      return frame_entry;
    }
  /* Create a new frame table entry */
  frame_entry
      = new_frame_table_entry (kernal_page, thread_tid (), table_entry);
  /* New frame table entry failed */
  if (!frame_entry)
    {
      /* Avoid memory leak */
      palloc_free_page (kernal_page);
      return NULL;
    }
  /* Add to frame table */
  list_push_back (&frame_table, &frame_entry->elem);
  return frame_entry;
}

/* Action func used in frame free page */
static bool
do_frame_entry_frame (frame_table_entry_t *entry)
{
  list_remove (&entry->elem);
  palloc_free_page (entry->frame);
  free (entry);
  return true;
}

/* Free a page and update the frame table */
void
frame_free_page (void *page)
{
  /* Invalid */
  if (!page)
    return;
  frame_table_foreach_if (frame_table_entry_equal_page, page,
                          do_frame_entry_frame);
}

frame_table_entry_t *
evict_one_frame ()
{
  /* choose a frame entry based on LRU */
  struct list_elem *min_elem
      = list_min (&frame_table, frame_access_time_less, NULL);
  frame_table_entry_t *frame
      = list_entry (min_elem, frame_table_entry_t, elem);
  struct thread *cur = thread_current ();
  frame->sup_table_entry->from_file = false;
  pagedir_clear_page (cur->pagedir, frame->sup_table_entry->addr);
  write_frame_to_block (frame);
  return frame;
}

bool
frame_access_time_less (const struct list_elem *a, const struct list_elem *b,
                        void *aux UNUSED)
{
  frame_table_entry_t *frame_a = list_entry (a, frame_table_entry_t, elem);
  frame_table_entry_t *frame_b = list_entry (b, frame_table_entry_t, elem);
  sup_page_table_entry_t *page_a = frame_a->sup_table_entry;
  sup_page_table_entry_t *page_b = frame_b->sup_table_entry;
  bool less_than = page_a->access_time < page_b->access_time;
  if (page_a->writable != page_b->writable)
    {
      return page_a->writable;
    }
  if (is_kernel_vaddr (page_a->addr) != is_kernel_vaddr (page_b->addr))
    return !is_kernel_vaddr (page_a->addr);
  return less_than;
}