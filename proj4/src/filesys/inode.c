#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Zeros with size of BLOCK_SECTOR_SIZE */
static char zeros[BLOCK_SECTOR_SIZE];

/* Store indirect blocks */
struct indirect_inode_disk
{
  block_sector_t blocks[N_INDIRECT_BLOCKS];
};

/* Wrap block_read, then replace with bvffer_cache_read */
static void
read_wrapper (block_sector_t sector, void *buffer)
{
  block_read (fs_device, sector, buffer);
}

/* Wrap block_write, then replace with bvffer_cache_write */
static void
write_wrapper (block_sector_t sector, const void *buffer)
{
  block_write (fs_device, sector, buffer);
}

/* Load a indirect_inode_disk from sector */
static void
load_indirect_inode_disk (struct indirect_inode_disk *node,
                          block_sector_t sector)
{
  read_wrapper (sector, node);
}

/* Create inode_disk with `sectors` number of sectors */
static bool do_inode_create (struct inode_disk *node_disk, size_t sectors);
/* Close (destroy) inode_disk with `sectors` number of sectors */
static bool do_inode_close (struct inode_disk *node_disk, size_t sectors);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Get the sector num according to a index */
static block_sector_t
index_to_sector (const struct inode_disk *node_disk, off_t index)
{
  /* Inside the direct blocks */
  /* Just return the direct block directly */
  if (index < N_LEVEL0)
    return node_disk->direct_blocks[index];
  /* Inside level 1 */
  if (index < N_LEVEL1)
    {
      /* First exclude level 0, it has been done previously */
      index -= N_LEVEL0;
      struct indirect_inode_disk level0_nodes;
      /* Load the indirect block */
      load_indirect_inode_disk (&level0_nodes, node_disk->indirect_block);
      /* Get the sector in the indirect block */
      return level0_nodes.blocks[index];
    }
  /* Inside level 2 */
  if (index < N_LEVEL2)
    {
      /* First exclude level 0 and level 1 */
      /* They have been done in the previous cases */
      index -= N_LEVEL1;
      /* Load level 1 nodes (the first indirect level) */
      struct indirect_inode_disk level1_nodes;
      load_indirect_inode_disk (&level1_nodes,
                                node_disk->doubly_indirect_block);
      /* Should be located two steps, first (index / N) then -> (index % N) */
      int level1_idx = index / N_INDIRECT_BLOCKS;
      struct indirect_inode_disk level0_nodes;
      /* Load the second indirect level node */
      load_indirect_inode_disk (&level0_nodes,
                                level1_nodes.blocks[level1_idx]);
      int level0_idx = index % N_INDIRECT_BLOCKS;
      /* Get the sector in the second indirect nodes */
      return level0_nodes.blocks[level0_idx];
    }

  /* Not found */
  return -1;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  if (pos < 0 || pos >= inode->data.length)
    return -1;
  off_t idx = pos / BLOCK_SECTOR_SIZE;
  return index_to_sector (&inode->data, idx);
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = is_dir;
      /* Wrap to call inode create */
      if (do_inode_create (disk_inode, sectors))
        {
          /* Success, then need to write to disk */
          write_wrapper (sector, disk_inode);
          success = true;
        }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  /* Change to wrapper because of buffer cache */
  read_wrapper (inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          if (inode->data.length >= 0)
            {
              size_t sectors = bytes_to_sectors (inode->data.length);
              /* Wrapper call to close */
              do_inode_close (&inode->data, sectors);
            }
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          read_wrapper (sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          /* Change to wrapper because of buffer cache */
          read_wrapper (sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  /* Write after EOF */
  /* offset + size - 1 is the end of this write */
  if (byte_to_sector (inode, offset + size - 1) == -1u)
    {
      size_t sectors = bytes_to_sectors (offset + size);
      /* First need to extend the file */
      /* Just do inode create */
      if (!do_inode_create (&inode->data, sectors))
        return 0;
      /* Update the data length */
      inode->data.length = offset + size;
      /* Use wrapper because of buffer cache */
      write_wrapper (inode->sector, &inode->data);
    }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          write_wrapper (sector_idx, buffer + bytes_written);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left)
            read_wrapper (sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          write_wrapper (sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

/* Create a sector and init with zeros */
static bool
do_inode_create_sector (block_sector_t *sector,
                        struct indirect_inode_disk *node)
{
  /* Check whether we need to create */
  if (*sector == 0)
    {
      /* Get a free sector */
      if (!free_map_allocate (1, sector))
        return false;
      /* Init with zeros */
      write_wrapper (*sector, zeros);
    }
  /* If used as indirect node, then read the info */
  if (node)
    read_wrapper (*sector, node);
  return true;
}

/* Create inode_disk with `sectors` number of sectors */
static bool
do_inode_create (struct inode_disk *node_disk, size_t sectors)
{
  /* Too many sectors */
  if (sectors > (size_t)N_LEVEL2)
    {
      return false;
    }
  else if (sectors > (size_t)N_LEVEL1)
    {
      /* Do level 2 */
      /* First create N_LEVEL1 sectors */
      do_inode_create (node_disk, N_LEVEL1);
      /* Remain sectors */
      sectors -= N_LEVEL1;
      /* L2node -> 128 L1 node */
      struct indirect_inode_disk level1_nodes;
      if (!do_inode_create_sector (&node_disk->doubly_indirect_block,
                                   &level1_nodes))
        return false;
      /* Enumerate L1 nodes */
      for (size_t l1_i = 0; l1_i < N_INDIRECT_BLOCKS && sectors > 0; ++l1_i)
        {
          struct indirect_inode_disk level0_nodes;
          /* L1 node -> 128 l0 node */
          if (!do_inode_create_sector (&level1_nodes.blocks[l1_i],
                                       &level0_nodes))
            return false;
          /* Remain sectors = min(sectors, 128) */
          size_t remain = N_INDIRECT_BLOCKS;
          if (sectors < remain)
            remain = sectors;
          /* Do allocation in L0 nodes */
          for (size_t l0_i = 0; l0_i < remain; ++l0_i)
            if (!do_inode_create_sector (&level0_nodes.blocks[l0_i], NULL))
              return false;
          /* The total remaining sectors */
          sectors -= remain;
          /* Record the level data */
          write_wrapper (level1_nodes.blocks[l1_i], &level0_nodes);
        }
      /* Record the doubly indirect block data */
      write_wrapper (node_disk->doubly_indirect_block, &level1_nodes);
    }
  else if (sectors > (size_t)N_LEVEL0)
    {
      /* Do level 1 */
      /* First create N_LEVEL0 sectors */
      do_inode_create (node_disk, N_LEVEL0);
      /* Remain sectors */
      sectors -= N_LEVEL0;
      /* L1node -> 128 L0 node */
      struct indirect_inode_disk level0_nodes;
      if (!do_inode_create_sector (&node_disk->indirect_block, &level0_nodes))
        return false;
      for (size_t l0_i = 0; l0_i < sectors; ++l0_i)
        if (!do_inode_create_sector (&level0_nodes.blocks[l0_i], NULL))
          return false;
      /* Record the level data */
      write_wrapper (node_disk->indirect_block, &level0_nodes);
    }
  else
    {
      /* Do level 0 */
      for (size_t i = 0; i < sectors; ++i)
        if (!do_inode_create_sector (&node_disk->direct_blocks[i], NULL))
          return false;
    }
  return true;
}

/* Close inode_disk with `sectors` number of sectors */
static bool
do_inode_close (struct inode_disk *node_disk, size_t sectors)
{
  if (sectors > (size_t)N_LEVEL2)
    {
      /* Too many sectors */
      return false;
    }
  else if (sectors > (size_t)N_LEVEL1)
    {
      /* Do level 2 */
      do_inode_close (node_disk, N_LEVEL1);
      sectors -= N_LEVEL1;
      struct indirect_inode_disk level1_nodes;
      load_indirect_inode_disk (&level1_nodes,
                                node_disk->doubly_indirect_block);
      /* Enumerate L1 nodes */
      for (size_t l1_i = 0; l1_i < N_INDIRECT_BLOCKS && sectors > 0; ++l1_i)
        {
          struct indirect_inode_disk level0_nodes;
          /* L1 node -> 128 l0 node */
          load_indirect_inode_disk (&level0_nodes, level1_nodes.blocks[l1_i]);
          /* Remain sectors = min(sectors, 128) */
          size_t remain = N_INDIRECT_BLOCKS;
          if (sectors < remain)
            remain = sectors;
          /* Do allocation in L0 nodes */
          for (size_t l0_i = 0; l0_i < remain; ++l0_i)
            free_map_release (level0_nodes.blocks[l0_i], 1);
          sectors -= remain;
          /* Free the meta data sector */
          free_map_release (level1_nodes.blocks[l1_i], 1);
        }
      /* Free the meta data sector */
      free_map_release (node_disk->doubly_indirect_block, 1);
    }
  else if (sectors > (size_t)N_LEVEL0)
    {
      /* Do level 1 */
      do_inode_close (node_disk, N_LEVEL0);
      /* Remain sectors */
      sectors -= N_LEVEL0;
      /* L1node -> 128 L0 node */
      struct indirect_inode_disk level0_nodes;
      load_indirect_inode_disk (&level0_nodes, node_disk->indirect_block);
      for (size_t l0_i = 0; l0_i < sectors; ++l0_i)
        free_map_release (level0_nodes.blocks[l0_i], 1);
      /* Free the meta data sector */
      free_map_release (node_disk->indirect_block, 1);
    }
  else
    {
      /* Do level 0 */
      for (size_t i = 0; i < sectors; ++i)
        free_map_release (node_disk->direct_blocks[i], 1);
    }
  return true;
}