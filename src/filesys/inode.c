#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define INODE_INDIRECT_MAGIC 0x68995383
#define INODE_DOUBLY_MAGIC 0x66312117
#define SECTOR_ERROR 0xffffffff



struct inode_indirect
  {
    disk_sector_t start;
    unsigned magic;
    disk_sector_t sector[126];
  };

struct inode_double
  {
    disk_sector_t start;
    unsigned magic;
    disk_sector_t inode_indirect[126];
  };

struct BC
{
  char buffer[DISK_SECTOR_SIZE];
  disk_sector_t sector;
  bool alloc;
  bool access;
  bool dirty;
  struct lock lock;
};

static void _disk_read (struct disk *, disk_sector_t, void *, off_t, size_t);
static void _disk_write (struct disk *, disk_sector_t, const void *, off_t, size_t);
static disk_sector_t byte_to_sector2 (struct inode_disk *, off_t); 

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}


/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  return byte_to_sector2 (&inode->data, pos);
}

static disk_sector_t
byte_to_sector2 (struct inode_disk *data, off_t pos) 
{
  ASSERT (data != NULL);
  if (pos / DISK_SECTOR_SIZE < DIRECT_SECTOR)
  {
    disk_sector_t sector = data->sector[pos / DISK_SECTOR_SIZE];
    if (sector == SECTOR_ERROR || sector == 0xffffff)
    {
      static char zeros[DISK_SECTOR_SIZE];
      if (!free_map_allocate (1, &(data->sector[pos / DISK_SECTOR_SIZE]))) 
      {
        return SECTOR_ERROR;
      }
      sector = data->sector[pos / DISK_SECTOR_SIZE];
      _disk_write (filesys_disk, sector, zeros, 0, DISK_SECTOR_SIZE); 
    }
    _disk_write (filesys_disk, data->start, data, 0, DISK_SECTOR_SIZE);
    return sector;
  }
  else if (pos / DISK_SECTOR_SIZE < DIRECT_SECTOR + 5 * 126)
  {
    disk_sector_t sector = data->indirect[(pos / DISK_SECTOR_SIZE - DIRECT_SECTOR) / 126];
    struct inode_indirect *indirect = (struct inode_indirect *) malloc (sizeof (struct inode_indirect));
    if (sector == SECTOR_ERROR)
    {
      if (!free_map_allocate (1, &(data->indirect[(pos / DISK_SECTOR_SIZE - DIRECT_SECTOR) / 126])))
      {
        free (indirect);
        return SECTOR_ERROR;
      }
      sector = data->indirect[(pos / DISK_SECTOR_SIZE - DIRECT_SECTOR) / 126];
      indirect->start = sector;
      indirect->magic = INODE_INDIRECT_MAGIC;
      memset (indirect->sector, SECTOR_ERROR, 126 * 4);
      _disk_write (filesys_disk, sector, indirect, 0, DISK_SECTOR_SIZE); 
    }
    _disk_write (filesys_disk, data->start, data, 0, DISK_SECTOR_SIZE);
    _disk_read (filesys_disk, sector, indirect, 0, DISK_SECTOR_SIZE);

    ASSERT (indirect->magic == INODE_INDIRECT_MAGIC);

    sector = indirect->sector[(pos / DISK_SECTOR_SIZE - DIRECT_SECTOR) % 126]; 
    if (sector == SECTOR_ERROR)
    {
      static char zeros[DISK_SECTOR_SIZE];
      if (!free_map_allocate (1, &(indirect->sector[(pos / DISK_SECTOR_SIZE - DIRECT_SECTOR) % 126]))) 
      {
        free (indirect);
        return SECTOR_ERROR;
      }
      sector = indirect->sector[(pos / DISK_SECTOR_SIZE - DIRECT_SECTOR) % 126];
      _disk_write (filesys_disk, sector, zeros, 0, DISK_SECTOR_SIZE); 
    }
    _disk_write (filesys_disk, indirect->start, indirect, 0, DISK_SECTOR_SIZE);
    free (indirect);
    return sector;
  }
  else if (pos / DISK_SECTOR_SIZE < DIRECT_SECTOR + 5 * 126 + 126 * 126)
  {
    disk_sector_t sector = data->doubly_indirect;
    struct inode_double *doubly = (struct inode_double *) malloc (sizeof (struct inode_double));
    if (sector == SECTOR_ERROR)
    {
      if (!free_map_allocate (1, &(data->doubly_indirect)))
      {
        free (doubly);
        return SECTOR_ERROR;
      }
      sector = data->doubly_indirect;
      _disk_read (filesys_disk, sector, doubly, 0, DISK_SECTOR_SIZE);
      doubly->start = sector;
      doubly->magic = INODE_DOUBLY_MAGIC;
      memset (doubly->inode_indirect, SECTOR_ERROR, 126 * 4);
      _disk_write (filesys_disk, sector, doubly, 0, DISK_SECTOR_SIZE);
    }
    _disk_write (filesys_disk, data->sector, data, 0, DISK_SECTOR_SIZE);
    _disk_read (filesys_disk, sector, doubly, 0, DISK_SECTOR_SIZE);
    ASSERT (doubly->magic == INODE_DOUBLY_MAGIC);
    struct inode_indirect *indirect = (struct inode_indirect *) malloc (sizeof (struct inode_indirect));
    sector = doubly->inode_indirect[(pos / DISK_SECTOR_SIZE - DIRECT_SECTOR - 5 * 126) % (126 * 126)];
    if (sector == SECTOR_ERROR)
    {
      if (!free_map_allocate (1, &(doubly->inode_indirect[(pos / DISK_SECTOR_SIZE - DIRECT_SECTOR - 5 * 126) % (126 * 126)])))
      {
        free (doubly);
        free (indirect);
        return SECTOR_ERROR;
      }
      sector = doubly->inode_indirect[(pos / DISK_SECTOR_SIZE - DIRECT_SECTOR - 5 * 126) % (126 * 126)];
      indirect->start = sector;
      indirect->magic = INODE_INDIRECT_MAGIC;
      memset (indirect->sector, SECTOR_ERROR, 126 * 4);
      _disk_write (filesys_disk, sector, indirect, 0, DISK_SECTOR_SIZE); 
    }
    _disk_write (filesys_disk, doubly->start, doubly, 0, DISK_SECTOR_SIZE);
    free (doubly);

    _disk_read (filesys_disk, sector, indirect, 0, DISK_SECTOR_SIZE);

    ASSERT (indirect->magic == INODE_INDIRECT_MAGIC);
    sector = indirect->sector[(pos / DISK_SECTOR_SIZE - DIRECT_SECTOR - 5 * 126) % 126]; 
    if (sector == SECTOR_ERROR)
    {
      static char zeros[DISK_SECTOR_SIZE];
      if (!free_map_allocate (1, &(indirect->sector[(pos / DISK_SECTOR_SIZE - DIRECT_SECTOR - 5 * 126) % 126]))) 
      {
        free (indirect);
        return SECTOR_ERROR;
      }
      sector = indirect->sector[(pos / DISK_SECTOR_SIZE - DIRECT_SECTOR - 5 * 126) % 126];
      _disk_write (filesys_disk, sector, zeros, 0, DISK_SECTOR_SIZE); 
    }
    _disk_write (filesys_disk, indirect->start, indirect, 0, DISK_SECTOR_SIZE);
    free (indirect);
    return sector;
  }
  else
    return SECTOR_ERROR;
}

static void free_inode_disk (struct inode_disk *data)
{
  int i;
  struct inode_indirect *indirect = (struct inode_indirect *) malloc (sizeof (struct inode_indirect));
  for (i = 0; i < DIRECT_SECTOR; ++i)
  {
    if (data->sector[i] != SECTOR_ERROR)
    {
      free_map_release (data->sector[i], 1);
    }
  }
  for (i = 0; i < 5; ++i)
  {
    int j;
    if (data->indirect[i] != SECTOR_ERROR)
    {
      _disk_read (filesys_disk, data->indirect[i], indirect, 0, DISK_SECTOR_SIZE);
      ASSERT (indirect->magic == INODE_INDIRECT_MAGIC);
      for (j = 0; j < 126; ++j)
      {
        if (indirect->sector[j] != SECTOR_ERROR)
        {
          free_map_release (indirect->sector[j], 1);
        }
      }
      free_map_release (data->indirect[i], 1);
    }
  }
  if (data->doubly_indirect != SECTOR_ERROR)
  {
    struct inode_double *doubly = (struct inode_doubly *) malloc (sizeof (struct inode_double));
    _disk_read (filesys_disk, data->doubly_indirect, doubly, 0, DISK_SECTOR_SIZE);
    ASSERT (doubly->magic == INODE_DOUBLY_MAGIC);
    for (i = 0; i < 126; ++i)
    {
      int j;
      if (doubly->inode_indirect[i] != SECTOR_ERROR)
      {
        _disk_read (filesys_disk, doubly->inode_indirect[i], indirect, 0, DISK_SECTOR_SIZE);
        ASSERT (indirect->magic == INODE_INDIRECT_MAGIC);
        for (j = 0; j < 126; ++j)
        {
          if (indirect->sector[j] != SECTOR_ERROR)
          {
            free_map_release (indirect->sector[j], 1);
          }
        }
        free_map_release (doubly->inode_indirect[i], 1);
      }
    }
    free_map_release (data->doubly_indirect, 1);
    free (doubly);
  }
  free (indirect);
}



/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

static struct BC BClist[64];
static int next_BC_to_evict;
static struct lock evict_lock;

/* Initializes the inode module. */
void
inode_init (void) 
{
  int i;
  list_init (&open_inodes);
  for (i = 0; i < 64; ++i)
  {
    memset (BClist[i].buffer, 0, DISK_SECTOR_SIZE);
    BClist[i].alloc = false;
    BClist[i].access = false;
    BClist[i].dirty = false;
    BClist[i].sector = -1;
    lock_init (&(BClist[i].lock));
  }
  next_BC_to_evict = 0;
  lock_init (&evict_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = true;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  memset (disk_inode->sector, SECTOR_ERROR, DIRECT_SECTOR * 4);
  memset (disk_inode->indirect, SECTOR_ERROR, 5 * 4);
  disk_inode->doubly_indirect = SECTOR_ERROR;

  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      int i;
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->start = sector;
      disk_inode->is_dir = is_dir;
      _disk_write (filesys_disk, sector, disk_inode, 0, DISK_SECTOR_SIZE);
      for (i = 0; i < sectors; ++i)
      {
        if (byte_to_sector2 (disk_inode, i * DISK_SECTOR_SIZE) == SECTOR_ERROR)
          success = false;
      }
      if (!success)
      {
        free_inode_disk (disk_inode);
      }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) 
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
  _disk_read (filesys_disk, inode->sector, &inode->data, 0, DISK_SECTOR_SIZE);
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
disk_sector_t
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
          free_inode_disk (&(inode->data));
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
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      _disk_read (filesys_disk, sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
      
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

  if (inode_length (inode) < offset + size)
    inode->data.length = offset + size;
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;


      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      _disk_write (filesys_disk, sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

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

static void _disk_read (struct disk *d, disk_sector_t sec_no, void *buffer, off_t start, size_t end)
{
  int i;
  struct BC *bc;
  for (i = 0; i < 64; ++i)
  {
    bc = &BClist[i];
    lock_acquire (&bc->lock);
    if (bc->sector == sec_no)
    {
      bc->access = true;
      goto read;
    }
    lock_release (&bc->lock);
  }

  lock_acquire (&evict_lock);
  while (true)
  {
    bc = &BClist[next_BC_to_evict];
    next_BC_to_evict = (next_BC_to_evict + 1) % 64;
    lock_acquire (&bc->lock);
    if (!bc->alloc)
    {
      disk_read (d, sec_no, bc->buffer);
      bc->sector = sec_no;
      bc->alloc = true;
      bc->access = false;
      bc->dirty = false;
      lock_release (&evict_lock);
      goto read;
    }
    if (!bc->access)
    {
      if (bc->dirty)
      {
        disk_write (d, bc->sector, bc->buffer);
      }
      memset (bc->buffer, 0, DISK_SECTOR_SIZE);
      disk_read (d, sec_no, bc->buffer);
      bc->sector = sec_no;
      bc->alloc = true;
      bc->access = true;
      bc->dirty = false;
      lock_release (&evict_lock);
      goto read;
    }
    bc->access = false;
    lock_release (&bc->lock);
  }

read:
  memcpy (buffer, bc->buffer + start, end);
  lock_release (&bc->lock);
}

bool inode_is_dir (struct inode *inode)
{
  return inode->data.is_dir;
}

static void _disk_write (struct disk *d, disk_sector_t sec_no, const void *buffer, off_t start, size_t end)
{
  int i;
  struct BC *bc;
  for (i = 0; i < 64; ++i)
  {
    bc = &BClist[i];
    lock_acquire (&bc->lock);
    if (bc->sector == sec_no)
    {
      bc->dirty = true;
      goto write;
    }
    lock_release (&bc->lock);
  }

  lock_acquire (&evict_lock);
  while (true)
  {
    bc = &BClist[next_BC_to_evict];
    next_BC_to_evict = (next_BC_to_evict + 1) % 64;
    lock_acquire (&bc->lock);
    if (!bc->alloc)
    {
      disk_read (d, sec_no, bc->buffer);
      bc->dirty = true;
      lock_release (&evict_lock);
      goto write;
    }
    if (!bc->access)
    {
      if (bc->dirty)
      {
        disk_write (d, bc->sector, bc->buffer);
      }
      memset (bc->buffer, 0, DISK_SECTOR_SIZE);
      disk_read (d, sec_no, bc->buffer);
      bc->dirty = true;
      lock_release (&evict_lock);
      goto write;
    }
    bc->access = false;
    lock_release (&bc->lock);
  }

write:
  memcpy (bc->buffer + start, buffer, end);
  bc->sector = sec_no;
  disk_write (d, bc->sector, bc->buffer);
  bc->alloc = true;
  bc->access = true;
  lock_release (&bc->lock);
}

void
inode_backup (void)
{
  int i;
  struct BC *bc;
  for (i = 0; i < 64; ++i)
  {
    bc = &BClist[i];
    lock_acquire (&bc->lock);
    if (bc->alloc && bc->dirty)
    {
      disk_write (filesys_disk, bc->sector, bc->buffer);
    }
    lock_release (&bc->lock);
  }
}

