#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H
#include <stdbool.h>
#include "devices/block.h"

/* The buffer cache size, which is 64 */
#define MAX_BUFFER_CACHE_SIZE 64

struct buffer_cache
{
  bool inuse;                      /* Whether the entry is used */
  bool dirty;                      /* Whether cache is dirty */
  int64_t time;                    /* Last access time, used in LRU */
  block_sector_t sector;           /* Store the sector */
  uint8_t data[BLOCK_SECTOR_SIZE]; /* Block data */
};

/* Init the buffer cache */
void buffer_cache_init (void);
/* Close the buffer cache, write the cache into the disk */
void buffer_cache_close (void);
/* Read with buffer cache */
void buffer_cache_read (block_sector_t sector, void *buffer);
/* Write with buffer cache */
void buffer_cache_write (block_sector_t sector, const void *buffer);

#endif