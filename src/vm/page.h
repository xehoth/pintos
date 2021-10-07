#ifndef VM_PAGE_H
#define VM_PAGE_H
#include <stdbool.h>
#include <hash.h>

typedef struct sup_page_table_entry
{
  void *addr;                 /* User virtual address */
  uint64_t access_time;       /* Last access time for LRU */
  struct hash_elem hash_elem; /* Hash table elem for sup page table */
} sup_page_table_entry_t;

/* Try to get a page which the fault addr refers */
bool try_get_page (void *fault_addr);
/* Grow the stack with given required addr */
bool grow_stack (void *addr);

#endif