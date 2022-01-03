#include "vm/frame.h"
#include "vm/swap.h"
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "filesys/file.h"

extern struct lock filesys_lock;

/* Frame table list, store frame table entries */
static struct list frame_table;
/* Avoid race condition when frame table is modified concurrently */
static struct lock frame_table_lock;

frame_table_entry_t *
new_frame_table_entry (void *frame_addr, tid_t owner,
                       sup_page_table_entry_t *sup_entry)
{
  /* Allocate space for frame table entry */
  frame_table_entry_t *entry
      = (frame_table_entry_t *)malloc (sizeof (frame_table_entry_t));

  /* If malloc failed */
  if (!entry)
    return NULL;
  /* Initialize frame table entry */
  entry->frame_addr = frame_addr;
  entry->owner = owner;
  entry->sup_table_entry = sup_entry;
  return entry;
}

frame_table_entry_t *
frame_new_page (sup_page_table_entry_t *sup_entry)
{
  if (!sup_entry)
    return NULL;

  /* Allocate a new page from free space in memory */
  void *k_page = palloc_get_page (PAL_USER);
  frame_table_entry_t *frame_entry;
  if (!k_page)
    {
      lock_acquire (&sup_entry->lock);
      /* Evict one frame and reuse this frame table entry */
      frame_entry = evict_one_frame ();
      /* Set tid and sup table entry */
      frame_entry->owner = thread_tid ();
      frame_entry->sup_table_entry = sup_entry;
      lock_release (&sup_entry->lock);
      return frame_entry;
    }
  /* Create a new frame table entry */
  frame_entry = new_frame_table_entry (k_page, thread_tid (), sup_entry);
  /* If frame table entry allocation failed */
  if (!frame_entry)
    {
      palloc_free_page (k_page);
      return NULL;
    }
  lock_acquire (&frame_table_lock);
  list_push_back (&frame_table, &frame_entry->elem);
  lock_release (&frame_table_lock);
  return frame_entry;
}

/* Action func used in frame free page */
static bool
free_frame_table_entry (frame_table_entry_t *frame_entry)
{
  /* Remove frame entry from frame table */
  list_remove (&frame_entry->elem);
  /* Free corresponding frame */
  palloc_free_page (frame_entry->frame_addr);
  /* Free the space if frame table entry */
  free (frame_entry);
  return true;
}

void
frame_free_page (void *frame_addr)
{
  /* Iterate all frame table entry and free corresponding one */
  if (!frame_addr)
    return;
  frame_table_foreach_if (frame_table_entry_crspd_frame, frame_addr,
                          free_frame_table_entry);
}

/* For each element in frame table, do some actions in some conditions */
void
frame_table_foreach_if (frame_table_action_cmp if_cmp, void *cmp_val,
                        frame_table_action_func action_func)
{
  lock_acquire (&frame_table_lock);
  /* Iterate through frame table and do comparison one by one */
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

/* Compare entry->frame and frame addr */
bool
frame_table_entry_crspd_frame (frame_table_entry_t *entry, void *frame_addr)
{
  return entry->frame_addr == frame_addr;
}

/* Init frame table */
void
frame_table_init ()
{
  list_init (&frame_table);
  lock_init (&frame_table_lock);
}

frame_table_entry_t *
evict_one_frame ()
{
  /* Choose a frame based on LRU */
  struct list_elem *min_elem
      = list_min (&frame_table, frame_access_time_less, NULL);
  frame_table_entry_t *frame
      = list_entry (min_elem, frame_table_entry_t, elem);

  lock_acquire (&frame_table_lock);
  struct thread *cur = thread_current ();
  /* If from file and dirty, write back the changes */
  if (frame->sup_table_entry->from_file && frame->sup_table_entry->is_mmap
      && pagedir_is_dirty (cur->pagedir, frame->sup_table_entry->addr))
    {
      lock_acquire (&filesys_lock);
      /* Seek correct place and write back */
      file_seek (frame->sup_table_entry->file, frame->sup_table_entry->ofs);
      file_write (frame->sup_table_entry->file, frame->sup_table_entry->addr,
                  frame->sup_table_entry->read_bytes);
      lock_release (&filesys_lock);
    }
  else
    {
      /* Write the frame to swap space */
      frame->sup_table_entry->from_file = false;
      write_frame_to_block (frame);
    }
  pagedir_clear_page (get_thread (frame->owner)->pagedir,
                      frame->sup_table_entry->addr);
  lock_release (&frame_table_lock);
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