#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/pte.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "userprog/pagedir.h"
#include "vm/spage.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include <hash.h>

struct SPTE * find_spte_from_upage (uint32_t);
static unsigned spage_hash_func (const struct hash_elem *, void *);
static bool spage_less (const struct hash_elem *, const struct hash_elem *, void *);

struct hash spage_hash;
struct lock spage_lock;
struct lock spage_find_lock;
struct lock vm_lock;

void
spage_init (void)
{
  frame_init ();
  swap_init ();
  hash_init (&spage_hash, spage_hash_func, spage_less, NULL);
  lock_init (&spage_lock);
  lock_init (&spage_find_lock);
  lock_init (&vm_lock);
}

void *
spage_palloc (uint32_t *upage, enum palloc_flags flags, bool writable)
{
  struct SPTE *spte, hash_spte;
  struct frame_entry *f_e;
  struct hash_elem *hash_e;
  uint32_t *pte;
  
  pte = lookup_page (thread_current ()->pagedir, upage, true);
  ASSERT (pte != NULL);
  ASSERT ((*pte & PTE_P) == 0);

  f_e = frame_palloc (flags, pte);
  spte = (struct SPTE *) malloc (sizeof (struct SPTE));
  
  lock_acquire (&spage_lock);

  hash_spte.hash_key = (uint32_t) upage + (thread_current ()->tid);
  hash_e = hash_find (&spage_hash, &hash_spte.hash_elem);
  ASSERT (hash_e == NULL);

  spte->upage = upage;
  spte->kpage = f_e->kpage;
  spte->pte = pte;
  spte->swap = false;
  spte->frame_entry = f_e;
  spte->swap_entry = NULL;
  spte->writable = writable;
  spte->thread = thread_current ();
  spte->hash_key = (uint32_t) upage + thread_current ()->tid;
  hash_insert (&spage_hash, &spte->hash_elem);
  *pte = pte_create_user (f_e->kpage, writable);
  list_push_back (&thread_current ()->spagedir, &spte->elem);
  f_e->spte = spte;
  f_e->loaded = true;
  
  lock_release (&spage_lock);

  return f_e->kpage;
}

void *
spage_get_page (uint32_t upage)
{
  struct SPTE *spte;

  spte = find_spte_from_upage (upage);
  if (spte == NULL)
    return NULL;

  frame_vm_acquire ();
  if (spte->swap)
  {
    frame_vm_release ();
    swap_in (spte);
  }
  else
    frame_vm_release ();
  return spte->kpage;
}

void
spage_free_page (struct SPTE *spte)
{
  frame_vm_acquire ();
  if (spte->swap)
    swap_free (spte);
  else
    frame_free (spte->kpage, true);
  frame_vm_release ();
  free (spte);
}

void
spage_free_dir ()
{
  struct list_elem *e;
  struct SPTE *spte;

  lock_acquire (&spage_lock);
  while (!list_empty (&thread_current ()->spagedir))
  {
    e = list_begin (&thread_current ()->spagedir);
    spte = list_entry (e, struct SPTE, elem);
    hash_delete (&spage_hash, &spte->hash_elem);
    list_remove (e);
    spage_free_page (spte);
  }
  lock_release (&spage_lock);
}


  

void
frame_vm_acquire ()
{
  lock_acquire (&vm_lock);
}

void
frame_vm_release ()
{
  lock_release (&vm_lock);
}


  
struct SPTE *
find_spte_from_upage (uint32_t upage)
{
  struct SPTE *spte, hash_spte;
  struct frame_entry *f_e;
  struct hash_elem *hash_e;

  hash_spte.hash_key = ((uint32_t) upage) + (thread_current ()->tid);

  lock_acquire (&spage_find_lock);
  hash_e = hash_find (&spage_hash, &hash_spte.hash_elem);
  if (hash_e == NULL)
    spte = NULL;
  else
    spte = hash_entry (hash_e, struct SPTE, hash_elem);
  lock_release (&spage_find_lock);
  return spte;
}



static unsigned
spage_hash_func (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct SPTE *p = hash_entry (p_, struct SPTE, hash_elem);
  return hash_bytes ((uint32_t) &p->hash_key, sizeof p->hash_key);
}

static bool
spage_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
  const struct SPTE *a = hash_entry (a_, struct SPTE, hash_elem);
  const struct SPTE *b = hash_entry (b_, struct SPTE, hash_elem);

  return a->hash_key < b->hash_key;
}
