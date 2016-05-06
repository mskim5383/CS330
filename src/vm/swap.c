#include "threads/synch.h"
#include "threads/pte.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include <bitmap.h>
#include <hash.h>
#include <disk.h>

static unsigned swap_hash_func (const struct hash_elem *, void *);
static bool swap_less (const struct hash_elem *, const struct hash_elem *, void *);

struct bitmap *swap_pool;
struct hash swap_hash;
struct disk *swap_disk;
struct lock swap_lock;
size_t disk_count;

struct sector_hash
{
  uint32_t count;
  size_t sector;
  struct hash_elem elem;
};

void swap_init (void)
{
  swap_disk = disk_get(1, 1);
  swap_pool = bitmap_create ((disk_count = disk_size (swap_disk) / 8));
  bitmap_set_all (swap_pool, true);
  hash_init (&swap_hash, swap_hash_func, swap_less, NULL);
  lock_init (&swap_lock);
}

void swap_out (void)
{
  struct frame_entry *f_e;
  struct sector_hash *s_h;
  struct list_elem *e;
  uint32_t i, kpage;
  size_t disk_idx;

  //printf ("swap out\n");
  
  lock_acquire (&swap_lock);
  f_e = frame_next_evict ();
  ASSERT (f_e != NULL);

  frame_to_out (f_e);

  disk_idx = bitmap_scan_and_flip (swap_pool, 0, 1, true);
  lock_release (&swap_lock);

  if (disk_idx == BITMAP_ERROR)
    PANIC ("swap panic");


  s_h = (struct sector_hash *) malloc (sizeof (struct sector_hash));
  s_h->count = f_e->pte;
  s_h->sector = disk_idx;
  hash_insert (&swap_hash, &s_h->elem);


  kpage = f_e->kpage;
  *(f_e->pte) = *(f_e->pte) & ~PTE_P;

  for (i = 0; i < 8; i++)
  {
    disk_write (swap_disk, disk_idx * 8 + i, (uint32_t) kpage + i * 512);
  }
  palloc_free_page (kpage);
  //printf ("swap out end\n");
}

bool swap_in (uint32_t *addr)
{
  struct frame_entry *f_e;
  struct sector_hash s_h;
  struct hash_elem *h_e;
  struct list_elem *e;
  uint32_t i, *pte;
  uint32_t *kpage;
  size_t disk_idx;

  //printf ("swap_in %p\n", addr);

  pte = lookup_page (thread_current ()->pagedir, addr, false);

  if (pte == NULL)
  {
    //printf ("%p\n", addr);
    return false;
  }
  s_h.count = pte;
  h_e = hash_find (&swap_hash, &s_h.elem);

  if (h_e == NULL)
    return false;
  hash_delete (&swap_hash, h_e);

  f_e = frame_get_frame_entry (hash_entry (h_e, struct sector_hash, elem)->count);

  kpage = palloc_get_page (f_e->flags);
  while (kpage == NULL)
  {
    swap_out ();
    kpage = palloc_get_page (f_e->flags);
  }

  disk_idx = hash_entry (h_e, struct sector_hash, elem)->sector;
  for (i = 0; i < 8; i++)
    disk_read (swap_disk, disk_idx * 8 + i, (uint32_t) kpage + i * 512);

  free (hash_entry (h_e, struct sector_hash, elem));

  f_e->kpage = kpage;
  *(f_e->pte) = pte_create_user (kpage, f_e->writable);

  frame_to_in (f_e);
  

  bitmap_flip (swap_pool, disk_idx);
  return true;
}
  



static unsigned
swap_hash_func (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct sector_hash *p = hash_entry (p_, struct sector_hash, elem);
  return hash_bytes (&p->count, sizeof p->count);
}

static bool
swap_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
  const struct sector_hash *a = hash_entry (a_, struct sector_hash, elem);
  const struct sector_hash *b = hash_entry (b_, struct sector_hash, elem);

  return a->count < b->count;
}
