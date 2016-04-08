#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <list.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "devices/input.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static void syscall_handler (struct intr_frame *);
static int sys_write (int , const void *, unsigned);
static int sys_halt (void);
static int sys_exit (int);
static pid_t sys_exec (const char *);
static int sys_wait (pid_t);
static int sys_open (const char *);
static int sys_close (int);
static int sys_read (int, void *, unsigned);

static int allocate_fd ();
static struct file_fd *find_file_fd (int);

struct file_fd
{
  struct file *file;
  int fd;
  struct list_elem elem;
};

static int next_fd;
static struct lock fd_lock;


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  next_fd = 3;
  lock_init (&fd_lock);
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
    case SYS_EXEC:
      ret = sys_exec (*(p + 1));
      break;
    case SYS_WAIT:
      ret = sys_wait (*(p + 1));
      break;
    case SYS_OPEN:
      ret = sys_open (*(p + 1));
      break;
    case SYS_CLOSE:
      ret = sys_close (*(p + 1));
      break;
    case SYS_READ:
      ret = sys_read (*(p + 1), *(p + 2), *(p + 3));
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

static pid_t
sys_exec (const char *cmd_line)
{
  return process_execute (cmd_line);
} 

static int
sys_wait (pid_t pid)
{
  return process_wait(pid);
}

static int
sys_open (const char *file)
{
  struct file *f;
  struct file_fd *f_fd;
  int fd;

  if (file == NULL)
    return -1;
  f = filesys_open (file);
  if (f == NULL)
    return -1;
  fd = allocate_fd ();
  f_fd = (struct file_fd *) malloc (sizeof (struct file_fd));
  f_fd->file = f;
  f_fd->fd = fd;
  list_push_back (&thread_current ()->file_list, &f_fd->elem);
  return fd;
}  

static int
sys_close (int fd)
{
  struct list_elem *e;
  struct file_fd *f_fd;

  f_fd = find_file_fd (fd);
  if (f_fd != NULL)
  {
    list_remove (&f_fd->elem);
    file_close (f_fd->file);
    free (f_fd);
  }
  return 0;
}

static int
sys_read (int fd, void *buffer, unsigned size)
{
  struct file_fd *f_fd;
  int ret, i;
  
  ret = -1;

  if (fd == STDIN_FILENO)
  {
    for (i = 0; i < size; i++)
      *(char *)(buffer + i) = input_getc (); 
    ret = size;
  }
  else
  {
    f_fd = find_file_fd (fd);
    if (f_fd == NULL)
      return -1;
    ret = file_read (f_fd->file, buffer, size);
  }
  return ret;
}

static int
allocate_fd ()
{
  int fd;
  lock_acquire (&fd_lock);
  fd = next_fd++;
  lock_release (&fd_lock);
  return fd;
}

static struct file_fd *
find_file_fd (int fd)
{
  struct list_elem *e;
  struct list *file_list;
  struct file_fd *f_fd;

  file_list = &thread_current ()->file_list;
  for (e = list_begin (file_list); e != list_end (file_list); e = list_next (e))
  {
    f_fd = list_entry (e, struct file_fd, elem);
    if (f_fd->fd == fd)
    {
      return f_fd;
    }
  }
  return NULL;
}
