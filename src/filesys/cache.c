#include "filesys/cache.h"
#include "threads/synch.h"
#include "devices/timer.h"
#include <string.h>

/* Caches, size with 64 */
static struct buffer_cache caches[MAX_BUFFER_CACHE_SIZE];
/* Buffer cache lock for synchronization */
static struct lock buffer_cache_lock;
/* Get the fs_device in the filesys.c */
extern struct block *fs_device;

/* Init a buffer cache entry */
static void
buffer_cache_entry_init (struct buffer_cache *entry, block_sector_t sector)
{
  entry->inuse = false;
  entry->dirty = false;
  /* Record the sector */
  entry->sector = sector;
  /* Update the time to current timer ticks */
  entry->time = timer_ticks ();
  /* Load the data from the disk into the cache */
  block_read (fs_device, sector, entry->data);
}

/* Flush a cache entry into the disk if exists and dirty */
static void
buffer_cache_entry_flush (struct buffer_cache *entry)
{
  /* Only when dirty need to write back */
  if (entry->inuse && entry->dirty)
    block_write (fs_device, entry->sector, entry->data);
}

/* Evict one entry with LRU, return the index */
static int
buffer_cache_evict_one (void)
{
  /* Init with the first entry */
  int64_t min_time = caches[0].time;
  int index = 0;
  /* Find the min access time */
  for (int i = 1; i < MAX_BUFFER_CACHE_SIZE; ++i)
    {
      /* Compare the access time and record the min */
      if (caches[i].time < min_time)
        {
          min_time = caches[i].time;
          index = i;
        }
    }
  /* Write back */
  buffer_cache_entry_flush (&caches[index]);
  return index;
}

/* Try to find an entry with given sector if no, insert one */
static int
buffer_cache_get (block_sector_t sector)
{
  /* Initialize with -1 for not found */
  int index = -1, free_index = -1;
  /* Loop through the cahche to find */
  for (int i = 0; i < MAX_BUFFER_CACHE_SIZE; ++i)
    {
      /* Find a free cache entry */
      if (free_index == -1 && !caches[i].inuse)
        free_index = i;
      /* Find the cache entry whose sector equals to the `sector` */
      if (caches[i].sector == sector)
        {
          index = i;
          break;
        }
    }
  /* Found the given sector */
  if (index != -1)
    {
      /* Update access time */
      caches[index].time = timer_ticks ();
      return index;
    }

  /* Cache is full, need eviction */
  if (free_index == -1)
    free_index = buffer_cache_evict_one ();
  /* Init the evicted entry */
  buffer_cache_entry_init (&caches[free_index], sector);
  return free_index;
}

/* Init buffer cache */
void
buffer_cache_init ()
{
  lock_init (&buffer_cache_lock);
  memset (caches, 0, sizeof (caches));
}

/* Close buffer cache */
void
buffer_cache_close ()
{
  lock_acquire (&buffer_cache_lock);
  /* Flush all */
  for (int i = 0; i < MAX_BUFFER_CACHE_SIZE; ++i)
    buffer_cache_entry_flush (caches + i);
  lock_release (&buffer_cache_lock);
}

/* Read with buffer cache */
void
buffer_cache_read (block_sector_t sector, void *buffer)
{
  lock_acquire (&buffer_cache_lock);
  /* Get the entry with given sector */
  int index = buffer_cache_get (sector);
  /* Just need to copy data in the cache to the buffer */
  memcpy (buffer, caches[index].data, BLOCK_SECTOR_SIZE);
  lock_release (&buffer_cache_lock);
}

/* Write with buffer cache */
void
buffer_cache_write (block_sector_t sector, const void *buffer)
{
  lock_acquire (&buffer_cache_lock);
  /* Get the corresponding cache entry */
  int index = buffer_cache_get (sector);
  /* After write the cache should be dirty */
  caches[index].dirty = true;
  /* Just need to copy data in the buffer into the cache */
  /* When flushing, write the data into the disk */
  memcpy (caches[index].data, buffer, BLOCK_SECTOR_SIZE);
  lock_release (&buffer_cache_lock);
}
