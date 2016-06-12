#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/thread.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  filesys_disk = disk_get (0, 1);
  if (filesys_disk == NULL)
    PANIC ("hd0:1 (hdb) not present, file system initialization failed");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  dir_init ();
  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  disk_sector_t inode_sector = 0;
  struct dir *dir = dir_open (inode_open (thread_current ()->dir)), *_dir;
  char last_name[128];
  
  if (dir != NULL)
    _dir = dir_lookup_dir (dir, name, true, last_name);
  bool success = (dir != NULL
                  && _dir != NULL
                  && strnlen (last_name, 128) <= 14
                  && strcmp (last_name, "root")
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (_dir, last_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
  dir_close (_dir);

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
  struct dir *dir = dir_open (inode_open (thread_current ()->dir)), *_dir;
  struct inode *inode = NULL;
  char last_name[14];

  if (dir != NULL)
  {
    _dir = dir_lookup_dir (dir, name, true, last_name);
    if (_dir != NULL)
    {
      if (!strcmp (last_name, "root"))
        inode = dir_get_inode (_dir);
      else
        dir_lookup (_dir, last_name, &inode);
      dir_close (_dir);
    }
  }
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open (inode_open (thread_current ()->dir)), *_dir = NULL;
  char last_name[14];
  
  if (dir != NULL)
    _dir = dir_lookup_dir (dir, name, true, last_name);
  bool success = _dir != NULL && dir_remove (_dir, last_name);
  dir_close (dir); 
  dir_close (_dir);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  struct dir *root;
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  root = dir_open (inode_open (ROOT_DIR_SECTOR));
  dir_add (root, ".", ROOT_DIR_SECTOR);
  dir_add (root, "..", ROOT_DIR_SECTOR);
  printf ("done.\n");
}
