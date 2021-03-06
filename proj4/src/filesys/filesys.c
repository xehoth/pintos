#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/thread.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();
  buffer_cache_init ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  free_map_close ();
  buffer_cache_close ();
}

/* Open dir with path (exclude the last token), save the last into file_name */
static struct dir *
dir_open_with_path (const char *name, char const **file_name)
{
  /* Empty file */
  if (*name == '\0')
    return NULL;
  struct dir *dir = NULL;
  struct inode *inode = NULL;
  if (*name == '/')
    {
      /* Absolute path */
      dir = dir_open_root ();
      /* Skip all leading / */
      /* Process cases like '///////...' */
      while (*name && *name == '/')
        ++name;
    }
  else
    {
      /* Relative path */
      struct thread *cur = thread_current ();
      /* Open the current working directory */
      dir = !cur->cwd ? dir_open_root () : dir_reopen (cur->cwd);
    }
  /* Invalid, the starting directory is NULL */
  if (!dir)
    return NULL;

  /* Loop through the path splited by '/' */
  for (const char *next_token = name;; name = next_token)
    {
      /* Split the path by '/' */
      while (*next_token && *next_token != '/')
        ++next_token;
      /* Reach the end, this is the last token */
      if (*next_token == '\0')
        {
          /* Then save the last token into file_name */
          *file_name = name;
          break;
        }
      /* Split current name */
      /* By utilizing C99 standard, which allows variable-length array */
      char cur_name[next_token - name + 1];
      memcpy (cur_name, name, next_token - name);
      /* Ensure this is a string by adding '\0' at the end */
      cur_name[next_token - name] = '\0';

      struct dir *next_dir = NULL;
      /* Failed to get file */
      if (!dir_lookup (dir, cur_name, &inode)
          || !(next_dir = dir_open (inode)))
        {
          /* Prevent memory leak */
          dir_close (dir);
          return NULL;
        }
      /* Close the current dir and move to the next */
      dir_close (dir);
      dir = next_dir;
      /* Skip the '/'s */
      while (*next_token && *next_token == '/')
        ++next_token;
    }
  /* Cannot open a removed dir */
  if (dir_get_inode (dir)->removed)
    {
      /* Prevent memory leak */
      dir_close (dir);
      return NULL;
    }

  return dir;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir)
{
  block_sector_t inode_sector = 0;
  const char *file_name = NULL;
  struct dir *dir = dir_open_with_path (name, &file_name);
  bool success = (dir != NULL && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, file_name, inode_sector, is_dir));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  const char *file_name = NULL;
  /* Open the directory that contains the file */
  struct dir *dir = dir_open_with_path (name, &file_name);
  if (!dir)
    return NULL;
  struct inode *inode = NULL;
  if (strlen (file_name) > 0)
    {
      /* Need to open a file in the dir */
      dir_lookup (dir, file_name, &inode);
      dir_close (dir);
    }
  else
    {
      /* No rest string, get self */
      inode = dir_get_inode (dir);
    }
  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  const char *file_name = NULL;
  /* Open the directory of the file */
  struct dir *dir = dir_open_with_path (name, &file_name);
  bool success = dir != NULL && dir_remove (dir, file_name);
  dir_close (dir);

  return success;
}

bool
filesys_chdir (const char *name)
{
  const char *file_name = NULL;
  /* Open the directory except the last token */
  struct dir *dir = dir_open_with_path (name, &file_name);
  if (!dir)
    return false;
  if (strlen (name) > 0)
    {
      struct inode *inode = NULL;
      /* Need to open the final dir in the dir */
      dir_lookup (dir, file_name, &inode);
      dir_close (dir);
      dir = dir_open (inode);
      if (!dir)
        return false;
    }
  struct thread *cur = thread_current ();
  /* Close the current working directory */
  dir_close (cur->cwd);
  /* Record the new current working directory */
  cur->cwd = dir;
  return true;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
