#ifndef VM_PAGE_H
#define VM_PAGE_H
#include <debug.h>
#include <stdbool.h>
#include <hash.h>

typedef struct hash sup_page_table_t;

typedef struct sup_page_table_entry
{
  void *addr;                 /* User virtual address */
  uint64_t access_time;       /* Last access time for LRU */
  struct hash_elem hash_elem; /* Hash table elem for sup page table */
} sup_page_table_entry_t;

/* Allocate and init a new sup table entry */
void *new_sup_table_entry (void *addr, uint64_t access_time);

/* Init the sup page table */
bool sup_table_init (sup_page_table_t *table);
/* Free and clean up all elements in the table */
void sup_table_free (sup_page_table_t *table);

/* Hash func for pages used in sup page table */
unsigned page_hash_func (const struct hash_elem *elem, void *aux UNUSED);
/* Less cmp func used for pages in sup page table */
bool page_less_func (const struct hash_elem *a, const struct hash_elem *b,
                     void *aux UNUSED);

/* Try to get a page which the fault addr refers */
bool try_get_page (void *fault_addr);
/* Grow the stack with given required addr */
bool grow_stack (void *addr);
#endif