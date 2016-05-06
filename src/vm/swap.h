#ifndef VM_SWAP_H
#define VM_SWAP_H

void swap_init (void);
void swap_out (void);
bool swap_in (uint32_t *);
void swap_acquire (void);
void swap_release (void);


#endif
