#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/pte.h"
#include "vm/spage.h"
#include "vm/mmap.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <list.h>


struct list mmap_list;
struct lock mmap_lock;
struct lock mapid_lock;
mapid_t mapid;

struct mmap_entry *find_mmap_by_upage(uint32_t *);
struct mmap_entry *find_mmap_by_mid(mapid_t);
mapid_t mapid_alloc (void);

struct mmap_entry
{
  mapid_t mapid;
  uint32_t *upage;
  struct thread *thread;
  struct SPTE *spte;
  struct list_elem elem;
};

void mmap_init (void)
{
  list_init (&mmap_list);
  lock_init (&mmap_lock);
  lock_init (&mapid_lock);
  mapid = 1;
}


mapid_t mmap_map (struct file *file, void *upage)
{
  uint32_t *kpage;
  int i;
  off_t length = file_length (file), ofs = 0;
  struct mmap_entry *m_e;
  mapid_t mid;
  
  if (length == 0)
    return -1;
  file = file_reopen (file);
  lock_acquire (&mmap_lock);
  for (i = 0; i < (length - 1) / PGSIZE + 1; ++i)
  {
    if (find_mmap_by_upage ((uint32_t) upage + (i * PGSIZE)) != NULL)
    {
      lock_release (&mmap_lock);
      return -1;
    }
  }
  mid = mapid_alloc ();
  for (i = 0; i < (length - 1) / PGSIZE; ++i)
  {
    spage_palloc (upage, PAL_USER | PAL_ZERO, true, true, ofs, PGSIZE, file, true);
    m_e = (struct mmap_entry *) malloc (sizeof (struct mmap_entry));
    m_e->mapid = mid;
    m_e->upage = upage;
    m_e->thread = thread_current ();
    ASSERT ((m_e->spte = find_spte_from_upage (upage)) != NULL);
    list_push_back (&mmap_list, &m_e->elem);
    
    ofs += PGSIZE;
    upage += PGSIZE;
  }
  spage_palloc (upage, PAL_USER | PAL_ZERO, true, true, ofs, length - length / PGSIZE * PGSIZE, file, true);
  m_e = (struct mmap_entry *) malloc (sizeof (struct mmap_entry));
  m_e->mapid = mid;
  m_e->upage = upage;
  m_e->thread = thread_current ();
  ASSERT ((m_e->spte = find_spte_from_upage (upage)) != NULL);
  list_push_back (&mmap_list, &m_e->elem);

  lock_release (&mmap_lock);
  return mid;
}

void mmap_unmap (mapid_t mid)
{
  struct mmap_entry *m_e;
  struct file *file;

  lock_acquire (&mmap_lock);
  while ((m_e = find_mmap_by_mid (mid)) != NULL)
  {
    list_remove (&m_e->elem);
    if ((*(m_e->spte->pte) & PTE_D) != 0)
    {
      if (m_e->spte->swap)
        swap_in (m_e->spte, false);
      file = file_reopen (m_e->spte->file);
      ASSERT (file != NULL);
      file_seek (file, m_e->spte->ofs);
      file_write (file, m_e->spte->kpage, m_e->spte->page_read_bytes);
      file_close (file);
    }
    spage_free_page (m_e->spte);
    free (m_e);
  }
  lock_release (&mmap_lock);
}

bool mmap_free (uint32_t *upage)
{
  struct mmap_entry *m_e;

  m_e = find_mmap_by_upage (upage);
  if (m_e == NULL)
    return false;
  mmap_unmap (m_e->mapid);
  return true;
}

struct mmap_entry *find_mmap_by_upage(uint32_t *upage)
{
  struct mmap_entry *m_e;
  struct list_elem *e;

  for (e = list_begin (&mmap_list); e != list_end (&mmap_list); e = list_next(e))
  {
    m_e = list_entry (e, struct mmap_entry, elem);
    if (m_e->upage == upage && m_e->thread == thread_current ())
    {
      return m_e;
    }
  }
  return NULL;
}

struct mmap_entry *find_mmap_by_mid(mapid_t mid)
{
  struct mmap_entry *m_e;
  struct list_elem *e;

  for (e = list_begin (&mmap_list); e != list_end (&mmap_list); e = list_next(e))
  {
    m_e = list_entry (e, struct mmap_entry, elem);
    if (m_e->mapid == mid)
    {
      return m_e;
    }
  }
  return NULL;
}


mapid_t mapid_alloc (void)
{
  mapid_t ret;

  lock_acquire (&mapid_lock);
  ret = mapid++;
  lock_release (&mapid_lock);

  return ret;
}
  
