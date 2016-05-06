#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include <hash.h>
#include <list.h>

static bool install_page (void *, void *, bool);
static unsigned page_hash (const struct hash_elem *, void *);
static bool page_less (const struct hash_elem *, const struct hash_elem *, void *);

static struct list frame_table;
static struct list frame_table_out;
static struct hash kpage_hash;
static struct lock frame_alloc_lock;


struct kpage_f_e
{
  uint32_t *pte;
  struct frame_entry *frame_entry;
  struct hash_elem elem;
};

void
frame_init (void)
{
  list_init (&frame_table);
  list_init (&frame_table_out);
  hash_init (&kpage_hash, page_hash, page_less, NULL);
  lock_init (&frame_alloc_lock);
}

uint8_t *
frame_get_page (enum palloc_flags flags, uint8_t *upage, bool writable)
{
  uint32_t *pte;
  uint8_t *kpage;
  struct frame_entry *f_e;
  struct kpage_f_e k_f_e, *temp_k_f_e;
  struct hash_elem *h_e;

  lock_acquire (&frame_alloc_lock);
  kpage = palloc_get_page (flags);
  while (kpage == NULL)
  {
    swap_out ();
    kpage = palloc_get_page (flags);
  }
  lock_release (&frame_alloc_lock);

  if (!install_page (upage, kpage, writable)) 
  {
    palloc_free_page (kpage);
    return NULL;
  }
  pte = lookup_page (thread_current ()->pagedir, upage, true);
  k_f_e.pte = pte;
  h_e = hash_find (&kpage_hash, &k_f_e.elem);
  if (h_e == NULL)
  {
    f_e = (struct frame_entry *) malloc (sizeof (struct frame_entry));
    f_e->kpage = kpage;
    f_e->pte = pte;
    f_e->flags = flags;
    f_e->writable = writable;
    temp_k_f_e = (struct kpage_f_e *) malloc (sizeof (struct kpage_f_e));
    temp_k_f_e->pte = pte;
    temp_k_f_e->frame_entry = f_e;
    hash_insert (&kpage_hash, &temp_k_f_e->elem);
    list_push_back (&frame_table, &f_e->elem);
  }
  else
  {
    PANIC ("frame: already mapped");
  }
  return kpage;
}

void
frame_free_page (uint32_t *pte)
{
  struct kpage_f_e k_f_e, *temp_k_f_e;
  struct frame_entry *f_e;
  struct hash_elem *h_e;
  struct list_elem *e;
  uint32_t *page;


  k_f_e.pte = pte;
  h_e = hash_find (&kpage_hash, &k_f_e.elem);
  if (h_e != NULL)
  {
    temp_k_f_e = hash_entry (h_e, struct kpage_f_e, elem);
    f_e = temp_k_f_e->frame_entry;
    page = f_e->kpage;
    hash_delete (&kpage_hash, h_e);
    list_remove (&f_e->elem);
    free (f_e);
    free (temp_k_f_e);
    palloc_free_page (page);
  }
  else
    PANIC ("frame: already freed");
}

struct frame_entry *
frame_next_evict ()
{
  struct frame_entry *f_e, *temp_f_e;
  struct list_elem *e, *e_pte;
  uint8_t *kpage = NULL;
  int accessed;

  f_e = NULL;
  e = list_begin (&frame_table);

  if (e == list_end (&frame_table))
    return f_e;
  while (f_e == NULL)
  {
    temp_f_e = list_entry (e, struct frame_entry, elem);
    accessed = *temp_f_e->pte & PTE_A;
    *temp_f_e->pte &= ~PTE_A;
    if (accessed == 0)
      f_e = temp_f_e;
    else
    {
      list_remove (e);
      list_push_back (&frame_table, e);
      e = list_begin (&frame_table);
    }
  }

  return f_e;
}

void
frame_hash_change (uint32_t *kpage, uint32_t *addr)
{
  struct kpage_f_e k_f_e, *temp_k_f_e;
  struct hash_elem *h_e;

  k_f_e.pte = addr;
  h_e = hash_find (&kpage_hash, &k_f_e.elem);

  if (h_e == NULL)
    PANIC ("frame: hash not exists");

  hash_delete (&kpage_hash, h_e);
  temp_k_f_e = hash_entry (h_e, struct kpage_f_e, elem);
  temp_k_f_e->pte = addr;
  hash_insert (&kpage_hash, &temp_k_f_e->elem);
}

struct frame_entry *
frame_get_frame_entry (uint32_t *pte)
{
  struct kpage_f_e k_f_e, *temp_k_f_e;
  struct hash_elem *h_e;

  k_f_e.pte = pte;
  h_e = hash_find (&kpage_hash, &k_f_e.elem);
  if (h_e == NULL)
    PANIC ("frame: hash not exists");
  temp_k_f_e = hash_entry (h_e, struct kpage_f_e, elem);
  return temp_k_f_e->frame_entry;
}




void
frame_to_in (struct frame_entry *f_e)
{
  list_remove (&f_e->elem);
  list_push_back (&frame_table, &f_e->elem);
}

void
frame_to_out (struct frame_entry *f_e)
{
  list_remove (&f_e->elem);
  list_push_back (&frame_table_out, &f_e->elem);
}
    

void
frame_reset_f_e (struct frame_entry *f_e)
{
  list_remove (&f_e->elem);
  list_push_back (&frame_table, &f_e->elem);
}

static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

static unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct kpage_f_e *p = hash_entry (p_, struct kpage_f_e, elem);
  return hash_bytes (&p->pte, sizeof p->pte);
}

static bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
  const struct kpage_f_e *a = hash_entry (a_, struct kpage_f_e, elem);
  const struct kpage_f_e *b = hash_entry (b_, struct kpage_f_e, elem);

  return a->pte < b->pte;
}

