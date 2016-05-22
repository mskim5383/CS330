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
  swap_pool = bitmap_create (swap_size = (disk_size (swap_disk) / 8 - 1));
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

  frame_vm_acquire ();
  lock_acquire (&swap_lock);

  f_e = frame_next_evict ();
  
  ASSERT (f_e != NULL);


  spte = f_e->spte;
  ASSERT (spte != NULL);
  bool lazy = spte->lazy;
  printf ("upage: %x lazy: %d dirty: %x\n", spte->upage, spte->lazy, *(spte->pte) & PTE_D);
  ASSERT (spte->pte == f_e->pte);
  ASSERT (spte != NULL);
  kpage = f_e->kpage;
  if ((!spte->lazy) || ((*(spte->pte)) & PTE_D))
  {
    printf ("(%s) swap out\n", thread_current ()->name);
    disk_idx = bitmap_scan_and_flip (swap_pool, 0, 1, true);
    printf ("(%s) swap out: %x\n", thread_current ()->name, disk_idx);
    if (disk_idx == BITMAP_ERROR)
      PANIC ("swap panic");

    s_e = (struct swap_entry *) malloc (sizeof (struct swap_entry));
    s_e->disk_idx = disk_idx;
    s_e->spte = f_e->spte;

    spte = f_e->spte;
    ASSERT (spte != NULL);
    kpage = f_e->kpage;
    for (i = 0; i < 8; i++)
    {
      disk_write (swap_disk, disk_idx * 8 + i, (uint32_t) kpage + i * 512);
    }
    spte->swap_entry = s_e;
    spte->lazy = false;
    printf ("(%s) swap out complete\n", thread_current ()->name);
  }
  spte->swap = true;
  frame_free (kpage, false);
  //*(spte->pte) &= ~PTE_P;
  ASSERT ((*(spte->pte) & PTE_P) == 0);

  lock_release (&swap_lock);
  frame_vm_release ();
  return kpage;
}

void
swap_in (struct SPTE *spte)
{
  struct frame_entry *f_e;
  uint32_t disk_idx, *kpage, i, *pte;
  struct file *file;
  bool lazy;


  ASSERT (spte->swap);
  ASSERT (thread_current () == spte->thread);

  f_e = frame_palloc (spte->flags, spte->pte);
  ASSERT (f_e != NULL);
  kpage = f_e->kpage;
  ASSERT (kpage != NULL);

  lock_acquire (&swap_lazy_lock);
  lazy = spte->lazy;
  if (spte->lazy)
  {
    //if (spte->read)
    {
      printf ("(%s) lazy loading %x\n", thread_current ()->name, spte->upage);
      file = spte->thread->file;
      ASSERT (file != NULL);
      file_seek (file, spte->ofs);
      ASSERT (file_read (file, kpage, spte->page_read_bytes) == spte->page_read_bytes);
      memset (kpage + spte->page_read_bytes, 0, PGSIZE - spte->page_read_bytes);
      printf ("(%s) lazy loading complete\n", thread_current ()->name);
    }
    /*
    else
    {
      printf ("(%s) zero loading %x\n", thread_current ()->name, spte->upage);
      //spte->lazy = false;
      memset (kpage, 0, PGSIZE);
      printf ("(%s) zero loading complete\n", thread_current ()->name);
    }
    */
    lock_release (&swap_lazy_lock);
  }
  else
  {
    printf ("(%s) swap in\n", thread_current ()->name);
    lock_release (&swap_lazy_lock);
    disk_idx = spte->swap_entry->disk_idx;
    printf ("(%s) swap in: %x %x\n", thread_current ()->name, disk_idx, spte->upage);
    for (i = 0; i < 8; i++)
      disk_read (swap_disk, disk_idx * 8 + i, (uint32_t) kpage + i * 512);
    printf ("(%s) swap in complete\n", thread_current ()->name);
  }
  spte->kpage = kpage;
  pte = spte->pte;
  ASSERT (f_e->spte == NULL);
  *pte = pte_create_user (f_e->kpage, spte->writable);
  f_e->spte = spte;
  f_e->loaded = true;
  if (!lazy)
  {
    bitmap_flip (swap_pool, disk_idx);
  }
  spte->swap = false;
  if (!lazy)
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
