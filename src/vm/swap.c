#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/pte.h"
#include "vm/spage.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <bitmap.h>
#include <disk.h>
#include <list.h>
#include <string.h>


struct bitmap *swap_pool;
struct disk *swap_disk;
struct lock swap_lock;
struct lock swap_lazy_lock;
size_t swap_size;

void
swap_init (void)
{
  swap_disk = disk_get(1, 1);
  swap_pool = bitmap_create (swap_size = (disk_size (swap_disk) / 8));
  bitmap_set_all (swap_pool, true);
  lock_init (&swap_lock);
  lock_init (&swap_lazy_lock);
}

uint32_t *
swap_out (void)
{
  struct frame_entry *f_e;
  struct SPTE *spte;
  struct swap_entry *s_e;
  uint32_t *kpage;
  uint32_t disk_idx, i;

  lock_acquire (&swap_lock);

  f_e = frame_next_evict ();
  
  ASSERT (f_e != NULL);

  spte = f_e->spte;
  kpage = f_e->kpage;

  if (!spte->lazy)
  {
    disk_idx = bitmap_scan_and_flip (swap_pool, 0, 1, true);
    if (disk_idx == BITMAP_ERROR)
      PANIC ("swap panic");

    s_e = (struct swap_entry *) malloc (sizeof (struct swap_entry));
    s_e->disk_idx = disk_idx;
    s_e->spte = f_e->spte;

    for (i = 0; i < 8; i++)
    {
      disk_write (swap_disk, disk_idx * 8 + i, (uint32_t) kpage + i * 512);
    }
    spte->swap_entry = s_e;
  }
  spte->swap = true;
  memset (kpage, 0, PGSIZE);
  frame_free (kpage, false);

  lock_release (&swap_lock);
  return kpage;
}

void
swap_in (struct SPTE *spte)
{
  struct frame_entry *f_e;
  uint32_t disk_idx, *kpage, i, *pte;


  ASSERT (spte->swap);
  ASSERT (thread_current () == spte->thread);

  f_e = frame_palloc (spte->flags, spte->pte);
  kpage = f_e->kpage;

  if (spte->lazy)
  {
    lock_acquire (&swap_lazy_lock);
    memset (kpage, 0, PGSIZE);
    file_seek (spte->thread->file, spte->ofs);
    if (file_read (spte->thread->file, kpage, spte->page_read_bytes) != (int) spte->page_read_bytes)
    {
      frame_free (kpage, true);
      PANIC ("lazy swap in fail");
    }
    lock_release (&swap_lazy_lock);
  }
  else
  {
    disk_idx = spte->swap_entry->disk_idx;
    for (i = 0; i < 8; i++)
      disk_read (swap_disk, disk_idx * 8 + i, (uint32_t) kpage + i * 512);
  }
  spte->kpage = kpage;
  pte = spte->pte;
  *pte = pte_create_user (f_e->kpage, spte->writable);
  f_e->spte = spte;
  f_e->loaded = true;
  if (!spte->lazy)
    bitmap_flip (swap_pool, disk_idx);
  spte->swap = false;
  if (!spte->lazy)
    free (spte->swap_entry);
}

void
swap_free (struct SPTE *spte)
{
  uint32_t disk_idx;

  ASSERT (spte->swap);
  ASSERT (thread_current () == spte->thread);

  disk_idx = spte->swap_entry->disk_idx;
  bitmap_flip (swap_pool, disk_idx);
  free (spte->swap_entry);
}
