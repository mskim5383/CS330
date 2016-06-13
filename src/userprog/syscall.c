#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <list.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/input.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/directory.h"
#include "filesys/inode.h"

static void syscall_handler (struct intr_frame *);
static int sys_write (int , const void *, unsigned);
static int sys_halt (void);
static int sys_exit (int);
static pid_t sys_exec (const char *);
static int sys_wait (pid_t);
static bool sys_create (const char *, unsigned);
static bool sys_remove (const char *);
static int sys_open (const char *);
static int sys_close (int);
static int sys_read (int, void *, unsigned);
static int sys_filesize (int);
static void sys_seek (int, unsigned);
static unsigned sys_tell (int);
static bool sys_chdir (const char *);
static bool sys_mkdir (const char *);
static bool sys_readdir (int, char *);
static bool sys_isdir (int);
static int sys_inumber (int);

static int allocate_fd ();
static struct file_fd *find_file_fd (int);
static int get_user (const uint8_t *);
static bool put_user (uint8_t *, uint8_t);
static bool pointer_checkvalid (void *, uint8_t);

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

  if (!pointer_checkvalid (p, 12))
    sys_exit (-1);
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
    case SYS_CREATE:
      ret = sys_create(*(p + 1), *(p + 2));
      break;
    case SYS_REMOVE:
      ret = sys_remove(*(p + 1));
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
    case SYS_FILESIZE:
      ret = sys_filesize (*(p + 1));
      break;
    case SYS_SEEK:
      sys_seek (*(p + 1), *(p + 2));
      break;
    case SYS_TELL:
      ret = sys_tell (*(p + 1));
      break;
    case SYS_CHDIR:
      ret = sys_chdir (*(p + 1));
      break;
    case SYS_MKDIR:
      ret = sys_mkdir (*(p + 1));
      break;
    case SYS_READDIR:
      ret = sys_readdir (*(p + 1), *(p + 2));
      break;
    case SYS_ISDIR:
      ret = sys_isdir (*(p + 1));
      break;
    case SYS_INUMBER:
      ret = sys_inumber (*(p + 1));
      break;
  }

  f->eax = ret;

  return;
}


static int
sys_write (int fd, const void *buffer, unsigned length)
{
  struct file_fd *f_fd;
  int ret;
  
  ret = -1;
  if (fd == STDOUT_FILENO)
  {
    putbuf (buffer, length);
    ret = length;
  }
  else
  {
    f_fd = find_file_fd (fd);
    if (f_fd == NULL)
      return ret;
    if(!pointer_checkvalid(buffer,1) || !pointer_checkvalid(buffer+length, 1))
      sys_exit (-1);
    if (inode_is_dir (f_fd->file->inode))
      return -1;
    
    lock_acquire (&fd_lock);
    ret = file_write (f_fd->file, buffer, length);
    lock_release (&fd_lock);
  }


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
  while (!list_empty(&thread_current ()->file_list))
    sys_close (list_entry (list_begin (&thread_current ()->file_list),
                                       struct file_fd, elem)->fd);
  thread_current ()->exit_status = status;
  sema_up (&thread_current ()->wait_child);
  thread_exit ();
  return -1;
}

static pid_t
sys_exec (const char *cmd_line)
{
  if (!pointer_checkvalid (cmd_line, strnlen(cmd_line, 128)))
    sys_exit (-1);
  return process_execute (cmd_line);
} 

static int
sys_wait (pid_t pid)
{
  return process_wait(pid);
}

static bool
sys_create (const char *file, unsigned initial_size)
{
  bool ret;
  if(!pointer_checkvalid(file, strnlen(file, 128)))
    sys_exit (-1);
  if (file == NULL)
    sys_exit (-1);
  lock_acquire (&fd_lock);
  ret = filesys_create (file, initial_size);
  lock_release (&fd_lock);
  return ret;
}

static bool
sys_remove (const char *file)
{
  bool ret;
  if (file == NULL)
    return sys_exit (-1);
  lock_acquire (&fd_lock);
  ret = filesys_remove (file);
  lock_release (&fd_lock);
  return ret;
}

static int
sys_open (const char *file)
{
  if(!pointer_checkvalid(file, strnlen(file, 128)))
    sys_exit(-1);
  struct file *f;
  struct file_fd *f_fd;
  int fd;

  if (file == NULL)
    return -1;
  lock_acquire (&fd_lock);
  f = filesys_open (file);
  lock_release (&fd_lock);
  if (f == NULL)
    return -1;
  fd = allocate_fd ();
  f_fd = (struct file_fd *) malloc (sizeof (struct file_fd));
  ASSERT (f_fd != NULL);
  f_fd->file = f;
  f_fd->fd = fd;
  list_push_back (&thread_current ()->file_list, &f_fd->elem);
  return fd;
}  

static int
sys_close (int fd)
{
  struct file_fd *f_fd;

  f_fd = find_file_fd (fd);
  if (f_fd != NULL)
  {
    list_remove (&f_fd->elem);
    lock_acquire (&fd_lock);
    file_close (f_fd->file);
    lock_release (&fd_lock);
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
    lock_acquire (&fd_lock);
    for (i = 0; i < size; i++)
      *(char *)(buffer + i) = input_getc (); 
    lock_release (&fd_lock);
    ret = size;
  }
  else
  {
    f_fd = find_file_fd (fd);
    if (f_fd == NULL)
      return -1;
    if(!pointer_checkvalid(buffer, 1) || !pointer_checkvalid(buffer + size, 1))
      sys_exit(-1);
    lock_acquire (&fd_lock);
    ret = file_read (f_fd->file, buffer, size);
    //printf ("ret %d size %d filesize %d\n", ret, size, sys_filesize (fd));
    lock_release (&fd_lock);
  }
  return ret;
}

static int
sys_filesize (int fd)
{
  struct file_fd *f_fd;
  f_fd = find_file_fd (fd);
  return file_length(f_fd->file);
}

static void
sys_seek (int fd, unsigned position)
{
  struct file_fd *f_fd;
  f_fd = find_file_fd (fd);
  file_seek(f_fd->file, position);
  return;
}

static unsigned
sys_tell (int fd)
{
  struct file_fd *f_fd;
  f_fd = find_file_fd (fd);
  return file_tell(f_fd->file); 
  
}

static bool
sys_chdir (const char *dir)
{
  if(!pointer_checkvalid(dir, strnlen(dir, 128)))
    sys_exit(-1);
  return dir_chdir (dir);
}

static bool 
sys_mkdir (const char *dir)
{
  if(!pointer_checkvalid(dir, strnlen(dir, 128)))
    sys_exit(-1);
  return dir_mkdir (dir);
}

static bool
sys_readdir (int fd, char *name)
{
  struct file_fd *f_fd;
  struct dir *dir;
  bool ret;

  if(!pointer_checkvalid(name, strnlen(name, 128)))
    sys_exit(-1);
  f_fd = find_file_fd (fd);
  if (f_fd == NULL)
    return false;
  if (!inode_is_dir (f_fd->file->inode))
    return false;
  dir = dir_open (f_fd->file->inode);
  dir->pos = f_fd->file->pos;

  ret = dir_readdir (dir, name);
  //printf ("readdir ret %d name %s\n", ret, name);
  f_fd->file->pos = dir->pos;

  return ret;
}

  




static bool
sys_isdir (int fd)
{
  struct file_fd *f_fd;
  f_fd = find_file_fd (fd);
  if (f_fd == NULL)
    return -1;
  return inode_is_dir (f_fd->file->inode);
}

static int
sys_inumber (int fd)
{
  struct file_fd *f_fd;
  f_fd = find_file_fd (fd);
  if (f_fd == NULL)
    return -1;
  return inode_get_inumber (f_fd->file->inode);
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

static int
get_user (const uint8_t *uaddr)
{
  if(!is_user_vaddr(uaddr))
    return -1;
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
      : "=&a" (result) : "m" (*uaddr));
  return result;
}

static bool
put_user (uint8_t *udst, uint8_t byte)
{
  if(!is_user_vaddr(udst))
    return false;
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
      : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

static bool
pointer_checkvalid (void * ptr, uint8_t byte)
{
  int i;
  for(i = 0; i <byte; i++)
  {
    if (get_user(((uint8_t*)(ptr))+i) == -1)
      return false;
  }
  return true;
}

void
sys_exit_extern (int status)
{
  sys_exit(status);
}
