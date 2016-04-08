#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);
static int sys_write (int , const void *, unsigned);
static int sys_halt (void);
static int sys_exit (int);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int *p;
  int ret;

  p = f->esp;
  switch (*p)
  {
    case SYS_WRITE:
      ret = sys_write (*(p + 1), *(p + 2), *(p + 3));
      break;
    case SYS_HALT:
      ret = sys_halt ();
      break;
    case SYS_EXIT:
      ret = sys_exit (*(p + 1));
      break;
  }

  f->eax = ret;

  return;
}


static int
sys_write (int fd, const void *buffer, unsigned length)
{
  int ret;

  ret = -1;
  if (fd != STDIN_FILENO)
    putbuf (buffer, length);

  return ret;
}

static int
sys_halt (void)
{
  power_off ();
}

static int
sys_exit (int status)
{
  thread_current ()->exit_status = status;
  sema_up (&thread_current ()->wait_child);
  thread_exit ();
  return -1;
}
