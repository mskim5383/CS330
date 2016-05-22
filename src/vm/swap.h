#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "vm/spage.h"

struct
swap_entry
{
  uint32_t disk_idx;
  struct SPTE *spte;
};

  


void swap_init (void);
uint32_t *swap_out (void);
void swap_in (struct SPTE *, bool);
void swap_free (struct SPTE *);

#endif
