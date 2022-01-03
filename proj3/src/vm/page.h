#ifndef VM_PAGE_H
#define VM_PAGE_H
#include <debug.h>
#include <hash.h>
#include <stdbool.h>
#include <stdint.h>
#include "threads/synch.h"

typedef struct hash sup_page_table_t;

typedef struct sup_page_table_entry
{
  void *addr;                 /* User virtual address */
  uint64_t access_time;       /* Latest time the page is accessed */
  struct hash_elem hash_elem; /* Hash table elem */
  int swap_idx;               /* Index of the begining sector in swap space */
  bool from_file;             /* Whether the page is from file */
  struct file *file;          /* File it belongs */
  int32_t ofs;                /* File pointer offset */
  uint32_t read_bytes;        /* Number of bytes read from file */
  uint32_t zero_bytes;        /* Empty zero bytes at end of the page */
  bool writable;              /* Whether the bage is writable */
  bool is_mmap;               /* Whether the page is mmap */
  struct lock lock;           /* Lock to synchronize */
} sup_page_table_entry_t;

/* try to get a page fault_addr referring to */
bool try_get_page (void *fault_addr, void *esp);
/* grow the stack */
bool grow_stack (void *fault_addr);

sup_page_table_entry_t *new_sup_table_entry (void *addr, uint64_t access_time);

/* Init the sup page table */
bool sup_table_init (sup_page_table_t *table);
/* Free and clean up all elements in the table */
void sup_table_free (sup_page_table_t *table);

/* Hash func for pages used in sup page table */
unsigned page_hash_func (const struct hash_elem *elem, void *aux UNUSED);
/* Less cmp func used for pages in sup page table */
bool page_less_func (const struct hash_elem *a, const struct hash_elem *b,
                     void *aux UNUSED);
/* Find matching sup table entry given page address */
sup_page_table_entry_t *sup_table_find (sup_page_table_t *table, void *page);

/* Load a page back to memory from swap space */
bool load_from_swap (void *addr, sup_page_table_entry_t *table_entry);
/* Lazy load, only create sup page table without allocate memory to it */
bool lazy_load (struct file *file, int32_t ofs, uint8_t *upage,
                uint32_t read_bytes, uint32_t zero_bytes, bool writable,
                bool is_mmap);

bool load_from_file (void *addr, sup_page_table_entry_t *table_entry);

#endif