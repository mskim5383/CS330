#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "vm/spage.h"

struct
frame_entry
{
  uint32_t *pte;
  void *kpage;
  struct thread *thread;
  struct list_elem elem;
  bool loaded;
  struct SPTE *spte;
};

void frame_init (void);
struct frame_entry *frame_palloc (enum palloc_flags, void *);
void frame_free (void *, bool);
struct frame_entry *frame_next_evict (void);

#endif
