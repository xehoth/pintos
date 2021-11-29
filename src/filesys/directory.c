#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

/* A directory. */
struct dir
{
  struct inode *inode; /* Backing store. */
  off_t pos;           /* Current position. */
};

/* A single directory entry. */
struct dir_entry
{
  block_sector_t inode_sector; /* Sector number of header. */
  char name[NAME_MAX + 1];     /* Null terminated file name. */
  bool in_use;                 /* In use or free? */
};

/* Search whethr name is already in dir, return -1 if exists, return offset */
/* that skips the exist entries */
static off_t lookup_and_offset (struct dir *dir, const char *name,
                                struct dir_entry *e);
/* Add a entry into the dir, return -1 for error, 0 for already exists */
/* 1 for success */
static int dir_add_entry (struct dir *dir, const char *name,
                          block_sector_t inode_sector);
/* Create a "." dir in the dir, return -1 for error, 0 for already exists */
/* 1 for success */
static int
dir_add_self_entry (struct dir *dir)
{
  if (!dir)
    return -1;
  return dir_add_entry (dir, ".", inode_get_inumber (dir->inode));
}

/* Create a ".." dir in the dir, return -1 for error, 0 for already exists */
/* 1 for success */
static int
dir_add_father_entry (struct dir *father, struct dir *child)
{
  if (!father || !child)
    return -1; /* Error */
  /* Ensure "." in child and father */
  if (dir_add_self_entry (child) < 0 || dir_add_self_entry (father) < 0)
    return -1; /* Error */
  return dir_add_entry (child, "..", inode_get_inumber (father->inode));
}
/* Check whether dir is empty */
static bool
dir_is_empty (struct dir *dir)
{
  struct dir_entry e;
  off_t ofs = 0;
  for (; inode_read_at (dir->inode, &e, sizeof (e), ofs) == sizeof (e);
       ofs += sizeof (e))
    {
      /* Exclude self and parent */
      if (e.in_use && !(!strcmp (".", e.name) || !strcmp ("..", e.name)))
        return false;
    }
  return true;
}
/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  if (!inode_create (sector, entry_cnt * sizeof (struct dir_entry), true))
    return false;

  struct dir *dir = dir_open (inode_open (sector));
  /* Ensure current dir contains "." (self) */
  bool success = dir_add_self_entry (dir) >= 0;
  dir_close (dir);
  return success;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode)
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      /* Ensure current dir contains "." (self) */
      if (dir_add_self_entry (dir) < 0)
        {
          inode_close (inode);
          free (dir);
          return NULL;
        }
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL;
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir)
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir)
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir)
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name, struct dir_entry *ep,
        off_t *ofsp)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (e.in_use && !strcmp (name, e.name))
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name, struct inode **inode)
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Search whethr name is already in dir, return -1 if exists, return offset */
/* that skips the exist entries */
static off_t
lookup_and_offset (struct dir *dir, const char *name, struct dir_entry *e)
{
  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    return -1; /* -1 for already exists */
  off_t ofs = 0;
  /* Set OFS to offset of free slot.
   If there are no free slots, then it will be set to the
   current end-of-file.

   inode_read_at() will only return a short read at end of file.
   Otherwise, we'd need to verify that we didn't get a short
   read due to something intermittent such as low memory. */
  for (; inode_read_at (dir->inode, e, sizeof (*e), ofs) == sizeof (*e);
       ofs += sizeof (*e))
    if (!e->in_use)
      break;
  return ofs;
}

/* Add a entry into the dir, return -1 for error, 0 for already exists */
/* 1 for success */
static int
dir_add_entry (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  /* Invalid name */
  if (!name || *name == '\0' || strlen (name) > NAME_MAX)
    return -1; /* -1 for error */
  struct dir_entry e;
  off_t ofs = lookup_and_offset (dir, name, &e);
  if (ofs == -1)
    return 0; /* 0 for alredy exists */
  /* Write slot */
  e.in_use = true;
  strlcpy (e.name, name, sizeof (e.name));
  e.inode_sector = inode_sector;
  if (inode_write_at (dir->inode, &e, sizeof (e), ofs) != sizeof (e))
    return -1; /* -1 for error */
  return 1;    /* 1 for success */
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector,
         bool is_dir)
{
  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  if (is_dir)
    {
      struct dir *child_dir = dir_open (inode_open (inode_sector));
      /* Cannot be already existed */
      bool success = dir_add_father_entry (dir, child_dir) > 0;
      dir_close (child_dir);
      if (!success)
        return false;
    }
  /* > 0 => cannot already contain */
  return dir_add_entry (dir, name, inode_sector) > 0;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name)
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Remove a dir */
  if (inode->data.is_dir)
    {
      struct dir *child_dir = dir_open (inode);
      bool is_empty = dir_is_empty (child_dir);
      dir_close (child_dir);
      if (!is_empty)
        goto done;
    }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e)
    {
      dir->pos += sizeof e;
      if (e.in_use && !(!strcmp (e.name, "..") || !strcmp (e.name, ".")))
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        }
    }
  return false;
}
