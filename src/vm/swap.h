#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdbool.h>
#include "vm/frame.h"

bool swap_init (void);
void swap_destroy (void);
void read_frame_from_block (frame_table_entry_t *frame, int sector_idx);
void write_frame_to_block (frame_table_entry_t *frame);
int get_new_swap_slot (void);

#endif