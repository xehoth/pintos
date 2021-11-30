#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H
#include <stdbool.h>
#include "devices/block.h"

#define MAX_BUFFER_CACHE_SIZE 64

struct buffer_cache
{
  bool inuse;                      /* Whether the entry is used */
  bool dirty;                      /* Whether cache is dirty */
  int64_t time;                    /* Last access time, used in LRU */
  block_sector_t sector;           /* Store the sector */
  uint8_t data[BLOCK_SECTOR_SIZE]; /* Block data */
};

void buffer_cache_init (void);
void buffer_cache_close (void);
void buffer_cache_read (block_sector_t sector, void *buffer);
void buffer_cache_write (block_sector_t sector, const void *buffer);

#endif