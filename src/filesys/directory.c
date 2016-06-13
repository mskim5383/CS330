#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"


/* A single directory entry. */
struct dir_entry 
  {
    disk_sector_t inode_sector;         /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };


void
dir_init (void)
{
  thread_current ()->dir = ROOT_DIR_SECTOR;
}

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (disk_sector_t sector, size_t entry_cnt) 
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), true);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      if (inode->sector > 10000)
      {
        free (dir);
        dir = dir_open_root ();
        return dir;
      }
      dir->inode = inode;
      dir->pos = 0;
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
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
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
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
  {
    *inode = inode_open (e.inode_sector);
  }
  else
    *inode = NULL;

  return *inode != NULL;
}

struct dir *
dir_lookup_dir (const struct dir *dir, const char *name, bool schubert, char *last_name)
{
  char _name[1000];
  char *token, *_token, *save_ptr;
  struct inode *inode;
  struct dir *_dir = dir_reopen (dir);
  bool success = true;
  strlcpy (_name, name, strnlen (name, 1000) + 1);
  if (strnlen (_name, 1000) == 0)
  {
    dir_close (_dir);
    return NULL;
  }

  if (_name[strnlen (_name, 1000) - 1] == '/')
  {
    _name[strnlen (_name, 1000) - 1] = '\0';
  }

  if (strnlen (_name, 1000) == 0)
  {
    dir_close (_dir);
    _dir = dir_open_root ();
    if (last_name != NULL)
      strlcpy (last_name, "root", strnlen ("root", 128) + 1);
    return _dir;
  }

  if (_name[0] == '/')
  {
    dir_close (_dir);
    _dir = dir_open_root ();
  }
  for (token = strtok_r (_name, "/", &save_ptr); token != NULL;
       token = _token)
  {
    _token = strtok_r (NULL, "/", &save_ptr);
    if (schubert && _token == NULL)
    {
      strlcpy (last_name, token, strnlen (token, 128) + 1);
      break;
    }
    if (!strcmp (token, ""))
    {
      continue;
    }
    else
    {
      if(dir_lookup (_dir, token, &inode))
      {
        dir_close (_dir);
        _dir = dir_open (inode);
        if (_dir == NULL)
        {
          success = false;
          break;
        }
      }
      else
      {
        success = false;
        break;
      }
    }
    token = _token;
  }

  if (success)
    return _dir;
  return NULL;
}

bool
dir_mkdir (const char *name)
{
  struct dir *dir = dir_open (inode_open (thread_current ()->dir)), *_dir, *new_dir;
  struct inode *inode;
  char last_name[14];
  bool success = true;
  disk_sector_t inode_sector = 0;

  _dir = dir_lookup_dir (dir, name, true, last_name);
  if (_dir == NULL)
  {
    success = false;
  }
  else
  {
    if (dir_lookup (_dir, last_name, &inode))
    {
      inode_close (inode);
      success = false;
    }
    else
    {
      if (free_map_allocate (1, &inode_sector))
      {
        if (dir_create (inode_sector, 0))
        {
          new_dir = dir_open (inode_open (inode_sector));
          dir_add (new_dir, ".", inode_sector);
          dir_add (new_dir, "..", _dir->inode->sector);
          dir_add (_dir, last_name, inode_sector);
          dir_close (new_dir);
        }
        else
        {
          success = false;
          free_map_release (inode_sector, 1);
        }
      }
      else
      {
        success = false;
      }
    }
    dir_close (_dir);
  }
  dir_close (dir);

  return success;
}

bool
dir_chdir (const char *name)
{
  struct dir *dir = dir_open (inode_open (thread_current ()->dir)), *_dir;
  struct inode *inode;
  char last_name[14];
  bool success = true;
  disk_sector_t new_dir;

  _dir = dir_lookup_dir (dir, name, false, NULL);
  if (_dir)
  {
    new_dir = inode_get_inumber (_dir->inode);
    thread_current ()->dir = new_dir;
    dir_close (_dir);
  }
  else
  {
    success = false;
  }
  dir_close (dir);

  return success;
}
  
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  //printf ("dir_readdir pos %d sector %d\n", dir->pos, dir->inode->sector);
  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (!strcmp (e.name, ".") || !strcmp(e.name, ".."))
        continue;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }

  return false;
}





        
  


/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, disk_sector_t inode_sector) 
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
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


  if (inode_is_dir (inode))
  {
    struct dir *dir = dir_open (inode);
    struct dir_entry _e;
    while (inode_read_at (dir->inode, &_e, sizeof _e, dir->pos) == sizeof _e) 
      {
        dir->pos += sizeof _e;
        if (_e.in_use && (strcmp (_e.name, ".") && strcmp(_e.name, "..")))
          {
            dir_close (dir);
            goto done;
          } 
      }
    dir_close (dir);
    if (inode->sector == thread_current ()->dir)
      goto done;
  }

  if (inode_is_dir (inode) && inode->open_cnt > 1)
    goto done;
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

void
dir_save (struct dir *dir)
{
  struct dir_entry e;

  dir->pos = 0;
  //printf ("dir_save pos %d sector %d\n", dir->pos, dir->inode->sector);
  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (!strcmp (e.name, ".") || !strcmp(e.name, ".."))
        continue;
      if (e.in_use)
        {
          struct dir *_dir = (struct dir *) malloc (sizeof (struct dir));
          //printf ("name %s\n", e.name);
          _dir -> pos = 0;
          _dir->inode = inode_open (e.inode_sector);
          if (inode_is_dir(_dir->inode))
            dir_save (_dir);
          free (_dir);
        } 
    }
}

