#ifndef VM_MMAP_H
#define VM_MMAP_H


#include "vm/spage.h"
#include "filesys/filesys.h"
#include "filesys/file.h"


typedef uint32_t mapid_t;

void mmap_init (void);
mapid_t mmap_map (struct file *, void *);
void mmap_unmap (mapid_t);
bool mmap_free (uint32_t *);



#endif
