#include "vm/swap.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "userprog/syscall.h"
#include <bitmap.h>

/* Swap table: tracking all free swap slots */
static struct bitmap *swap_table;
/* Lock for avoid race condition */
static struct lock swap_table_lock;
/* The swap block provided */
static struct block *global_swap_block;

/* initialize a swap block and swap table */
bool
swap_init ()
{
  global_swap_block = block_get_role (BLOCK_SWAP);
  swap_table = bitmap_create (block_size (global_swap_block));
  if (!swap_table)
    return false;
  lock_init (&swap_table_lock);
  return true;
}

void
swap_destroy ()
{
  bitmap_destroy (swap_table);
}

void
swap_release (int sector_idx)
{
  lock_acquire (&swap_table_lock);
  bitmap_set_multiple (swap_table, sector_idx, 8, false);
  lock_release (&swap_table_lock);
}

/* read a frame to FRAME from block at SECTOR_IDX */
void
read_frame_from_block (frame_table_entry_t *frame, int sector_idx)
{
  /* sector size is 512B and frame size is 4kB */
  for (int i = 0; i < 8; ++i)
    {
      /* read 8 consecutive sectors */
      block_read (global_swap_block, sector_idx + i,
                  frame->frame_addr + (i * BLOCK_SECTOR_SIZE));
    }
  /* mark those 8 sectors as unused */
  swap_release (sector_idx);
}

/* wrtie data in FRAME to en empyt slot */
void
write_frame_to_block (frame_table_entry_t *frame)
{
  int sector_idx = get_new_swap_slot ();
  frame->sup_table_entry->swap_idx = sector_idx;
  for (int i = 0; i < 8; ++i)
    {
      /* write to 8 consecutive sectors */
      block_write (global_swap_block, sector_idx + i,
                   frame->frame_addr + (i * BLOCK_SECTOR_SIZE));
    }
}

/* get a free swap slots */
int
get_new_swap_slot ()
{
  lock_acquire (&swap_table_lock);
  /* Find a emptyr slot and filp it to used */
  size_t sector = bitmap_scan_and_flip (swap_table, 0, 8, false);
  /* Exit if no empty space */
  if (sector == BITMAP_ERROR)
    syscall_exit (-1);
  lock_release (&swap_table_lock);
  return sector;
}