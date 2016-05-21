#include "threads/thread.h"
#include "threads/pte.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include <list.h>


void *find_entry_from_kpage (void *);

struct list frame_table;
struct lock frame_lock;
struct lock frame_entry_lock;
struct lock frame_size_lock;
uint32_t table_size;

#define FRAME_TABLE_SIZE 300


void
frame_init (void)
{
  list_init (&frame_table);
  lock_init (&frame_lock);
  lock_init (&frame_entry_lock);
  lock_init (&frame_size_lock);
  table_size = 0;
}

struct frame_entry *
frame_palloc (enum palloc_flags flags, void *pte)
{
  void *kpage = NULL;
  struct frame_entry *f_e;

  lock_acquire (&frame_size_lock);
  if (table_size <= FRAME_TABLE_SIZE)
    kpage = palloc_get_page (flags);
  table_size += 1;
  lock_release (&frame_size_lock);

  if (kpage == NULL)
    kpage = swap_out ();

  f_e = (struct frame_entry *) malloc (sizeof (struct frame_entry));
  ASSERT (f_e != NULL);

  lock_acquire (&frame_lock);
  f_e->pte = pte;
  f_e->kpage = kpage;
  f_e->thread = thread_current ();
  f_e->loaded = false;
  list_push_back (&frame_table, &f_e->elem);
  lock_release (&frame_lock);

  return f_e;
}

void
frame_free (void *kpage, bool palloc_free)
{
  struct frame_entry *f_e;

  lock_acquire (&frame_lock);
  f_e = find_entry_from_kpage (kpage);
  if (f_e == NULL)
  {
    lock_release (&frame_lock);
    return;
  }

  *(f_e->pte) &= ~PTE_P;
  lock_acquire (&frame_entry_lock);
  list_remove (&f_e->elem);
  lock_release (&frame_entry_lock);
  free (f_e);
  if (palloc_free)
    palloc_free_page (kpage);

  lock_release (&frame_lock);
  
  lock_acquire (&frame_size_lock);
  table_size -= 1;
  lock_release (&frame_size_lock);
}

struct frame_entry *
frame_next_evict (void)
{
  struct frame_entry *f_e;
  struct list_elem *e;
  uint32_t accessed;

  while (true)
  {
    e = list_begin (&frame_table);
    f_e = list_entry (e, struct frame_entry, elem);
    accessed = *(f_e->pte) & PTE_A;
    *(f_e->pte) &= ~PTE_A;
    if (accessed == 0 && f_e->loaded)
      return f_e;
    lock_acquire (&frame_entry_lock);
    list_remove (e);
    list_push_back (&frame_table, e);
    lock_release (&frame_entry_lock);
  }

  return NULL;
}
  

void *find_entry_from_kpage (void *kpage)
{
  struct list_elem *e;
  struct frame_entry *f_e, *ret;

  ret = NULL;

  lock_acquire (&frame_entry_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table); e = list_next (e))
  {
    f_e = list_entry (e, struct frame_entry, elem);
    if (f_e->kpage == kpage)
    {
      ret = f_e;
      break;
    }
  }
  lock_release (&frame_entry_lock);
  
  return ret;
}
