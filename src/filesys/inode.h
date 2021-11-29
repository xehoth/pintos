#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "list.h"

/* Number of direct blocks */
#define N_DIRECT_BLOCKS (128 - 3 - 2)
#define N_INDIRECT_BLOCKS 128

static const int N_LEVEL0 = N_DIRECT_BLOCKS;
static const int N_LEVEL1 = N_LEVEL0 + N_INDIRECT_BLOCKS;
static const int N_LEVEL2 = N_LEVEL1 + N_INDIRECT_BLOCKS * N_INDIRECT_BLOCKS;

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  union
  {
    block_sector_t blocks[N_DIRECT_BLOCKS + 2];
    struct
    {
      block_sector_t direct_blocks[N_DIRECT_BLOCKS]; /* Direct block sectors */
      block_sector_t indirect_block;
      block_sector_t doubly_indirect_block;
    };
  };
  off_t length;   /* File size in bytes. */
  bool is_dir;    /* Is directory */
  unsigned magic; /* Magic number. */
};

/* In-memory inode. */
struct inode
{
  struct list_elem elem;  /* Element in inode list. */
  block_sector_t sector;  /* Sector number of disk location. */
  int open_cnt;           /* Number of openers. */
  bool removed;           /* True if deleted, false otherwise. */
  int deny_write_cnt;     /* 0: writes ok, >0: deny writes. */
  struct inode_disk data; /* Inode content. */
};

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#endif /* filesys/inode.h */
