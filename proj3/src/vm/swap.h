#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdbool.h>
#include "vm/frame.h"

#define NOT_IN_SWAP (-1)

/* Initialize the swap space */
bool swap_init (void);
/* Deallocate all resources for swap space */
void swap_destroy (void);
/* Release corresponding slot in swap by given sector idx */
void swap_release (int sector_idx);
/* Read a frame frow disk */
void read_frame_from_block (frame_table_entry_t *frame, int sector_idx);
/* Write a frame frow disk */
void write_frame_to_block (frame_table_entry_t *frame);
/* Get a new swap slot */
int get_new_swap_slot (void);

#endif
