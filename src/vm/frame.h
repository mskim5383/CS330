#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "userprog/process.h"


void frame_init (void);
uint8_t *frame_get_page (enum palloc_flags, uint8_t *, bool);
void frame_free_page (uint32_t *);
struct frame_entry *frame_next_evict (void);
void frame_hash_change (uint32_t *, uint32_t *);
struct frame_entry *frame_get_frame_entry (uint32_t *);
void frame_to_in (struct frame_entry *);
void frame_to_out (struct frame_entry *);

struct frame_entry
{
  uint32_t *kpage;
  uint32_t *pte;
  struct list_elem elem;
  enum palloc_flag flags;
  bool writable;
};



#endif
