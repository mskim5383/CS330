#ifndef VM_SPAGE_H
#define VM_SPAGE_H

#include "threads/palloc.h"
#include "filesys/file.h"
#include <list.h>
#include <hash.h>

struct
SPTE
{
  uint32_t *upage;
  uint32_t *kpage;
  uint32_t *pte;
  bool swap;
  bool swapping;
  struct frame_entry *frame_entry;
  struct swap_entry *swap_entry;
  enum palloc_flags flags;
  bool writable;
  bool lazy;
  bool mmap;
  off_t ofs;
  size_t page_read_bytes;
  struct file *file;
  struct thread *thread;
  struct list_elem elem;
  struct hash_elem hash_elem;
  uint32_t hash_key; // upage + tid
};

void spage_init (void);
void *spage_palloc (uint32_t *, enum palloc_flags, bool, bool, off_t, size_t, struct file *, bool);
void *spage_get_page (uint32_t);
void spage_free_page (struct SPTE *);
void spage_free_dir (void);
void frame_vm_acquire (void);
void frame_vm_release (void);

#endif
