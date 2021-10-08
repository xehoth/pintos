#include "vm/swap.h"
#include "devices/block.h"

static struct block* global_swap_block;

void
swap_init ()
{
  global_swap_block = block_get_role (BLOCK_SWAP);
}