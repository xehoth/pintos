#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"

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
void *
frame_new_page (sup_page_table_entry_t *table_entry)
{
  /* Invalid */
  if (!table_entry)
    return NULL;
  void *kernal_page = palloc_get_page (PAL_USER);
  frame_table_entry_t *entry;
  if (!kernal_page)
    {
      /* evict one frame and reuse this frame table entry */
      entry = evict_one_frame ();
      /* set tid and sup table entry */
      entry->owner = thread_tid ();
      entry->sup_table_entry = table_entry;
      return entry->frame;
    }
  /* Create a new frame table entry */
  entry = new_frame_table_entry (kernal_page, thread_tid (), table_entry);
  /* New frame table entry failed */
  if (!entry)
    {
      /* Avoid memory leak */
      palloc_free_page (kernal_page);
      return NULL;
    }
  /* Add to frame table */
  list_push_back (&frame_table, &entry->elem);
  return kernal_page;
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
  frame_table_entry_t *entry; // TODO
  write_frame_to_block (entry);
  return entry;
}

frame_table_entry_t *
select_LRU ()
{
  // TODO
}