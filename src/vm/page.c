#include "vm/page.h"
#include "vm/frame.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/timer.h"

#define NOT_IN_SWAP (-1)

extern bool install_page (void *, void *, bool);

/* Allocate and init a new sup table entry */
void *
new_sup_table_entry (void *addr, uint64_t access_time)
{
  sup_page_table_entry_t *entry
      = (sup_page_table_entry_t *)malloc (sizeof (sup_page_table_entry_t));
  if (!entry)
    return NULL;
  entry->addr = pg_round_down (addr);
  entry->access_time = access_time;
  entry->swap_idx = NOT_IN_SWAP;
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

static sup_page_table_entry_t *
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
try_get_page (void *fault_addr)
{
  /* First try to find the sup page table */
  struct thread *cur = thread_current ();
  sup_page_table_entry_t *table_entry
      = sup_table_find (&cur->sup_page_table, fault_addr);
  /* If not: need to grow */
  if (!table_entry)
    {
      return grow_stack (fault_addr);
    }
  else
    {
      // TODO:
      // eviction
      // load page back
      
    }
  return false;
}

bool
grow_stack (void *addr)
{
  struct thread *cur = thread_current ();
  sup_page_table_entry_t *table_entry
      = new_sup_table_entry (addr, timer_ticks ());
  if (!table_entry)
    return false;
  void *kernel_page = frame_new_page (table_entry);
  /* Failed to get a new page */
  if (!kernel_page)
    {
      free (table_entry);
      return false;
    }
  bool success
      = install_page (table_entry->addr, kernel_page, true)
        && !hash_insert (&cur->sup_page_table, &table_entry->hash_elem);
  if (!success)
    {
      free (table_entry);
      frame_free_page (kernel_page);
      return false;
    }
  return true;
}

