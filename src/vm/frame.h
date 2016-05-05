#ifdef VM_PROCESS_H
#define VM_PROCESS_H

#include "userprog/process.h"


void frame_init (void);
uint8_t *frame_get_page (enum palloc_flags, uint8_t *, bool);
void frame_free_page (uint32_t *);

#endif
