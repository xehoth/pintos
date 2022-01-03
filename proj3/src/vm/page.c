#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "userprog/syscall.h"
#include "filesys/file.h"
#include <string.h>

extern struct lock filesys_lock;
extern bool install_page (void *, void *, bool);

sup_page_table_entry_t *
new_sup_table_entry (void *addr, uint64_t access_time)
{
  /* Malloc space for sup page table entry */
  sup_page_table_entry_t *entry
      = (sup_page_table_entry_t *)malloc (sizeof (sup_page_table_entry_t));

  /* Return NULL if malloc fail */
  if (!entry)
    return NULL;
  /* Initialize new allocated sup page table entry */
  entry->addr = pg_round_down (addr);
  entry->access_time = access_time;
  entry->swap_idx = NOT_IN_SWAP;
  entry->from_file = false;
  entry->file = NULL;
  entry->ofs = 0;
  entry->read_bytes = 0;
  entry->zero_bytes = 0;
  entry->writable = false;
  entry->is_mmap = false;
  lock_init (&entry->lock);
  return entry;
}

/* Init the sup page table */
bool
sup_table_init (sup_page_table_t *table)
{
  return hash_init (table, page_hash_func, page_less_func, NULL);
}

/* Free entry in the table */
static void
do_sup_table_entry_free (struct hash_elem *e, void *aux UNUSED)
{
  sup_page_table_entry_t *entry
      = hash_entry (e, sup_page_table_entry_t, hash_elem);
  /* Release corresponding swap space when necessary */
  if (entry->swap_idx != NOT_IN_SWAP)
    swap_release (entry->swap_idx);
  free (entry);
}

/* Free and clean up all elements in the table */
void
sup_table_free (sup_page_table_t *table)
{
  hash_destroy (table, do_sup_table_entry_free);
}

/* Hash func for pages used in sup page table */
unsigned
page_hash_func (const struct hash_elem *elem, void *aux UNUSED)
{
  const sup_page_table_entry_t *entry
      = hash_entry (elem, sup_page_table_entry_t, hash_elem);
  return hash_bytes (&entry->addr, sizeof (entry->addr));
}

/* Less cmp func used for pages in sup page table */
bool
page_less_func (const struct hash_elem *a, const struct hash_elem *b,
                void *aux UNUSED)
{
  const sup_page_table_entry_t *entry_a
      = hash_entry (a, sup_page_table_entry_t, hash_elem);
  const sup_page_table_entry_t *entry_b
      = hash_entry (b, sup_page_table_entry_t, hash_elem);
  return (uint32_t)entry_a->addr < (uint32_t)entry_b->addr;
}

sup_page_table_entry_t *
sup_table_find (sup_page_table_t *table, void *page)
{
  /* Invalid */
  if (!table || !page)
    return NULL;
  sup_page_table_entry_t entry;
  entry.addr = pg_round_down (page);
  struct hash_elem *found = hash_find (table, &entry.hash_elem);
  if (!found)
    return NULL;
  return hash_entry (found, sup_page_table_entry_t, hash_elem);
}

bool
try_get_page (void *fault_addr, void *esp)
{
  /* Try to find the page in sup page table */
  struct thread *cur = thread_current ();
  sup_page_table_entry_t *sup_entry
      = sup_table_find (&cur->sup_page_table, fault_addr);
  /* If not found, grow stack */
  if (!sup_entry)
    {
      if ((uint32_t)fault_addr < (uint32_t)esp - 32)
        return false;
      return grow_stack (fault_addr);
    }
  else if (sup_entry->from_file)
    {
      return load_from_file (fault_addr, sup_entry);
    }
  else
    {
      return load_from_swap (fault_addr, sup_entry);
    }
}

bool
grow_stack (void *fault_addr)
{
  struct thread *cur = thread_current ();
  /* Allocate new sup table entry */
  sup_page_table_entry_t *table_entry
      = new_sup_table_entry (fault_addr, timer_ticks ());
  if (!table_entry)
    return false;

  /* Allocate new frame */
  frame_table_entry_t *frame_entry = frame_new_page (table_entry);
  /* Free sup entry if new frame failed */
  if (!frame_entry)
    {
      free (table_entry);
      return false;
    }
  /* Install new kernel page if success */
  void *k_page = frame_entry->frame_addr;
  bool success
      = install_page (table_entry->addr, k_page, true)
        && !hash_insert (&cur->sup_page_table, &table_entry->hash_elem);
  /* Release resouces if install failed */
  if (!success)
    {
      free (table_entry);
      frame_free_page (k_page);
      return false;
    }
  return true;
}

bool
load_from_swap (void *addr, sup_page_table_entry_t *table_entry)
{
  /* Get a frame by eviction */
  frame_table_entry_t *frame = frame_new_page (table_entry);
  lock_acquire (&table_entry->lock);
  /* load data in swap space back to this frame */
  read_frame_from_block (frame, table_entry->swap_idx);
  /* Set new owner and sup_table_entry of the frame */
  table_entry->swap_idx = NOT_IN_SWAP;
  table_entry->access_time = timer_ticks ();
  bool success = install_page (table_entry->addr, frame->frame_addr,
                               table_entry->writable);
  if (!success)
    {
      frame_free_page (frame->frame_addr);
      hash_delete (&thread_current ()->sup_page_table,
                   &table_entry->hash_elem);
      lock_release (&table_entry->lock);
      return false;
    }
  lock_release (&table_entry->lock);
  return true;
}

bool
lazy_load (struct file *file, int32_t ofs, uint8_t *upage, uint32_t read_bytes,
           uint32_t zero_bytes, bool writable, bool is_mmap)
{
  int32_t offset = ofs;
  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      sup_page_table_entry_t *sup_entry
          = new_sup_table_entry (upage, timer_ticks ());
      if (!sup_entry)
        return false;
      /* Initialize new sup page */
      sup_entry->from_file = true;
      sup_entry->file = file;
      sup_entry->read_bytes = page_read_bytes;
      sup_entry->zero_bytes = page_zero_bytes;
      sup_entry->writable = writable;
      sup_entry->ofs = offset;
      sup_entry->is_mmap = is_mmap;

      /* Add sup table entry into sup table */
      struct thread *cur = thread_current ();
      if (hash_insert (&cur->sup_page_table, &sup_entry->hash_elem))
        {
          free (sup_entry);
          return false;
        }

      /* Advance */
      offset += page_read_bytes;
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

bool
load_from_file (void *addr, sup_page_table_entry_t *table_entry)
{
  /* Try to get a new frame */
  frame_table_entry_t *frame_entry = frame_new_page (table_entry);
  if (!frame_entry)
    return false;
  lock_acquire (&table_entry->lock);
  void *kernel_page = frame_entry->frame_addr;
  /* Read content from file */
  lock_acquire (&filesys_lock);
  file_seek (table_entry->file, table_entry->ofs);
  if (file_read (table_entry->file, kernel_page, table_entry->read_bytes)
      != (int)table_entry->read_bytes)
    {
      frame_free_page (kernel_page);
      lock_release (&filesys_lock);
      lock_release (&table_entry->lock);
      return false;
    }
  lock_release (&filesys_lock);
  /* Initialize remaining part of page to 0 */
  memset (kernel_page + table_entry->read_bytes, 0, table_entry->zero_bytes);
  /* Install page  to page table*/
  if (!install_page (table_entry->addr, kernel_page, table_entry->writable))
    {
      frame_free_page (kernel_page);
      lock_release (&table_entry->lock);
      return false;
    }
  lock_release (&table_entry->lock);
  return true;
}