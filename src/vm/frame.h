#ifndef VM_FRAME_H
#define VM_FRAME_H
#include <stdint.h>
#include <list.h>
#include "vm/page.h"

typedef struct frame_table_entry
{
  void *frame;           /* Frame address (Physical) */
  struct list_elem elem; /* List elem used in frame table */
} frame_table_entry_t;

#endif