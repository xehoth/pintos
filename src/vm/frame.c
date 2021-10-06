#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"

/* Frame table list, store frame table entries */
static struct list frame_table;
