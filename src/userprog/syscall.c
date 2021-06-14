#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/exception.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/thread.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"

typedef void syscall_func (struct intr_frame* UNUSED);

/* Local variables */
syscall_func *sys_func[20];
struct lock filesys_lock;
int fd_cnt = 5;

static void syscall_handler (struct intr_frame *);
void mem_scanf(void *src, void *des, int size);
void check_buffer(char *file_name, bool check_whole);
void check_ptr (void *uaddr);
void system_exit (int exit_code);

struct file * find_by_fd(int fd);

/* Functions prototype */
void sys_halt (struct intr_frame * UNUSED);
void sys_exit (struct intr_frame * UNUSED);
void sys_exec (struct intr_frame * UNUSED);
void sys_wait (struct intr_frame * UNUSED);
void sys_create (struct intr_frame * UNUSED);
void sys_remove (struct intr_frame * UNUSED);
void sys_open (struct intr_frame * UNUSED);
void sys_filesize (struct intr_frame * UNUSED);
void sys_read (struct intr_frame * UNUSED);
void sys_write (struct intr_frame * UNUSED);
void sys_seek(struct intr_frame * UNUSED);
void sys_tell(struct intr_frame * UNUSED);
void sys_close(struct intr_frame * UNUSED);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  sys_func[SYS_HALT] = sys_halt;
  sys_func[SYS_EXIT] = sys_exit;
  sys_func[SYS_EXEC] = sys_exec;
  sys_func[SYS_WAIT] = sys_wait;
  sys_func[SYS_CREATE] = sys_create;
  sys_func[SYS_REMOVE] = sys_remove;
  sys_func[SYS_OPEN] = sys_open;
  sys_func[SYS_FILESIZE] = sys_filesize;
  sys_func[SYS_READ] = sys_read;
  sys_func[SYS_WRITE] = sys_write;
  sys_func[SYS_SEEK] = sys_seek;
  sys_func[SYS_TELL] = sys_tell;
  sys_func[SYS_CLOSE] = sys_close;

  lock_init (&filesys_lock);

}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int code;
  check_ptr (f->esp);
  mem_scanf(f->esp, &code, sizeof(code));

  if(code < 0 || code >= SYS_MAX_CALL){
    system_exit (-1);
  }
  // printf("---request %d", code);
  sys_func[code](f);
}

/* Prototype
void
halt (void) 
{
  syscall0 (SYS_HALT);
  NOT_REACHED ();
} 

Terminates Pintos by calling shutdown_power_off() (declared in devices/shutdown.h).
 This should be seldom used, because you lose some information 
 about possible deadlock situations, etc.*/

void
sys_halt (struct intr_frame *f UNUSED) 
{
  shutdown_power_off();
}

/* Prototype
void
exit (int status)
{
  syscall1 (SYS_EXIT, status);
  NOT_REACHED ();
} 

Terminates the current user program, returning status to the kernel. 
If the process's parent waits for it (see below), this is the status that will be returned.
Conventionally, a status of 0 indicates success and nonzero values indicate errors.*/

void
sys_exit (struct intr_frame *f UNUSED) 
{
  // f->eax = *(int)
  int status;
  check_ptr (f->esp + 4);
  mem_scanf(f->esp + 4, &status, sizeof(status));
  
  system_exit (status);
}

void 
system_exit (int exit_code){
  struct thread *cur = thread_current();
  cur->exit_status = exit_code;
  
  char *file_name = cur->name;
  int i;
  // printf("<----exit %s (%d)\n", file_name, exit_code);
  for(i = 0; file_name[i] && file_name[i] != ' '; i++);
  file_name[i] = 0;
  printf("%s: exit(%d)\n",file_name, exit_code );

  struct list *root = &cur->files_list;
  while (!list_empty(root)){
    struct file *file_ptr = list_entry (list_pop_front(root), struct file, elem);
     file_close (file_ptr);
  }

  thread_exit();
}

/* Prototype 
pid_t
exec (const char *file)
{
  return (pid_t) syscall1 (SYS_EXEC, file);
} 

Runs the executable whose name is given in cmd_line, passing any given arguments,
 and returns the new process's program id (pid). Must return pid -1,
  which otherwise should not be a valid pid, if the program cannot load or run for any reason. 
  Thus, the parent process cannot return from the exec until it knows whether the child process 
  successfully loaded its executable. You must use appropriate synchronization to ensure this.*/


void
sys_exec (struct intr_frame *f UNUSED) 
{
  char *file_name;
  check_ptr (f->esp + 4);
  mem_scanf(f->esp + 4, &file_name, sizeof(file_name));
  check_buffer (file_name, true);
  // printf("----->%s will exec %s\n", thread_current()->name, file_name);
  f->eax = process_execute(file_name);
}
/* Prototype 
int
wait (pid_t pid)
{
  return syscall1 (SYS_WAIT, pid);
} */

void
sys_wait (struct intr_frame *f UNUSED) 
{
  int pid;

  check_ptr (f->esp + 4);
  mem_scanf(f->esp + 4, &pid, sizeof(pid));
  f->eax = process_wait(pid);
}

/* Prototype 
bool
create (const char *file, unsigned initial_size)
{
  return syscall2 (SYS_CREATE, file, initial_size);
} 

Creates a new file called file initially initial_size bytes in size. 
Returns true if successful, false otherwise. Creating a new file does not open it: 
opening the new file is a separate operation which would require a open system call.
*/

void
sys_create (struct intr_frame *f UNUSED) 
{
  char *file;
  unsigned initial_size;

  check_ptr (f->esp + 8);
  mem_scanf (f->esp + 4, &file, sizeof(file));
  mem_scanf (f->esp + 8, &initial_size, sizeof(initial_size));

  check_buffer (file, true);

  lock_acquire (&filesys_lock);
  f->eax = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
}

/* Prototype 
bool
remove (const char *file)
{
  return syscall1 (SYS_REMOVE, file);
} 

Deletes the file called file. Returns true if successful, false otherwise. 
A file may be removed regardless of whether it is open or closed, and 
removing an open file does not close it.
*/

void
sys_remove (struct intr_frame *f UNUSED) 
{ 
  char *file;
  check_ptr (f->esp + 4);
  mem_scanf (f->esp + 4, &file, sizeof(file));
  check_buffer (file, true);

  lock_acquire (&filesys_lock);
  f->eax = filesys_remove (file);
  lock_release (&filesys_lock);
}

/* Prototype 
int
open (const char *file)
{
  return syscall1 (SYS_OPEN, file);
} 

Opens the file called file. Returns a nonnegative integer handle 
called a "file descriptor" (fd), or -1 if the file could not be opened.
*/

void
sys_open (struct intr_frame *f UNUSED) 
{
  char *file_buffer;

  check_ptr (f->esp + 4);
  mem_scanf (f->esp + 4, &file_buffer, sizeof(file_buffer));

  check_buffer (file_buffer, true);

  lock_acquire (&filesys_lock);
  struct file *file_ptr = filesys_open (file_buffer);
  lock_release (&filesys_lock);

  if( file_ptr == NULL){
    f->eax = -1;
  } else{
    struct thread *cur = thread_current();
    file_ptr->fd = ++cur->fd_cnt;
    list_push_back (&cur->files_list, &file_ptr->elem);
    f->eax = file_ptr->fd;

  }
}

/* Prototype 
int
filesize (int fd) 
{
  return syscall1 (SYS_FILESIZE, fd);
} 

Returns the size, in bytes, of the file open as fd.
*/

void
sys_filesize (struct intr_frame *f UNUSED) 
{
  int fd;

  check_ptr (f->esp + 4);
  mem_scanf (f->esp + 4, &fd, sizeof(fd));
  struct file *file_ptr = find_by_fd (fd);
  if( file_ptr == NULL )
    system_exit (-1);

  lock_acquire (&filesys_lock);
  f->eax = file_length (file_ptr);
  lock_release (&filesys_lock);
}

/* Prototype 
int
read (int fd, void *buffer, unsigned size)
{
  return syscall3 (SYS_READ, fd, buffer, size);
} 

Reads size bytes from the file open as fd into buffer. Returns 
the number of bytes actually read (0 at end of file), 
or -1 if the file could not be read (due to a condition 
other than end of file). Fd 0 reads from the keyboard using input_getc().
*/

void
sys_read (struct intr_frame *f UNUSED) 
{
  int fd;
  char *buffer;
  unsigned size;

  check_ptr (f->esp + 12);
  mem_scanf(f->esp + 4, &fd, sizeof(fd));
  mem_scanf(f->esp + 8, &buffer, sizeof(buffer));
  mem_scanf(f->esp + 12, &size, sizeof(size));

  check_buffer (buffer, true);
  if (fd == 0){
    for(int i = 0; i < size; ++i)
      buffer[i] = input_getc();
    buffer[size] = 0;
    f->eax = size;
  } else {
    struct file *file_ptr = find_by_fd(fd);
    if(file_ptr == NULL){ 
      f->eax = -1;
      return;
    }

    lock_acquire (&filesys_lock);
    f->eax = file_read (file_ptr, buffer, size);
    lock_release (&filesys_lock);
  }
}

/* 
Prototype 
int
write (int fd, const void *buffer, unsigned size)
{
  return syscall3 (SYS_WRITE, fd, buffer, size);
} 

Writes size bytes from buffer to the open file fd. 
Returns the number of bytes actually written, which may be 
less than size if some bytes could not be written.
*/ 

void
sys_write (struct intr_frame *f UNUSED) 
{
  // // printf("case $---\n");
  // int *fd_ptr = (char *)f->esp + 4;
  // char **buffer_ptr = (char *)f->esp + 8;
  // unsigned *size_ptr = (char *)f->esp + 12;
  int fd;
  char *buffer;
  unsigned size;

  check_ptr (f->esp + 12);
  mem_scanf(f->esp + 4, &fd, sizeof(fd));
  mem_scanf(f->esp + 8, &buffer, sizeof(buffer));
  mem_scanf(f->esp + 12, &size, sizeof(size));
  check_buffer (buffer, false);

  if( fd == 1 ){
    size = size < 1000? size:1000;
    putbuf(buffer, size);
    f->eax = size;
  } else {
    struct file *file_ptr = find_by_fd(fd);

    if (file_ptr == NULL)
      system_exit (-1);

    lock_acquire (&filesys_lock);
    f->eax = file_write (file_ptr, buffer, size);
    lock_release (&filesys_lock);
  }
}

/* Prototype 
void
seek (int fd, unsigned position) 
{
  syscall2 (SYS_SEEK, fd, position);
} 

Changes the next byte to be read or written in open file fd to position,
expressed in bytes from the beginning of the file. (Thus, a position of 0 is the file's start.)
*/

void
sys_seek (struct intr_frame *f UNUSED) 
{
  int fd;
  unsigned position;
  check_ptr (f->esp + 8);
  mem_scanf (f->esp + 4, &fd, sizeof(fd));
  mem_scanf (f->esp + 8, &position, sizeof(position));

  struct file *file_ptr = find_by_fd(fd);

  if (file_ptr == NULL)
    system_exit (-1);
  file_seek (file_ptr, position);
}

/* Prototype 
unsigned
tell (int fd) 
{
 
  return syscall1 (SYS_TELL, fd);
} 

Returns the position of the next byte to be read or written in open file fd, 
expressed in bytes from the beginning of the file.
*/

void
sys_tell (struct intr_frame *f UNUSED) 
{
  int fd;
  check_ptr (f->esp + 4);
  mem_scanf (f->esp + 4, &fd, sizeof(fd));
  struct file *file_ptr = find_by_fd(fd);
  if (file_ptr == NULL)
    system_exit (-1);
  f->eax = file_tell (file_ptr);
}

/* Prototype 
void
close (int fd)
{
  syscall1 (SYS_CLOSE, fd);
} 

Closes file descriptor fd. Exiting or terminating a process implicitly 
closes all its open file descriptors, as if by calling this function for each one.
*/

void
sys_close (struct intr_frame *f UNUSED) 
{
  int fd;

  check_ptr (f->esp + 4);
  mem_scanf (f->esp + 4, &fd, sizeof(fd));
  struct file *file_ptr = find_by_fd(fd);
  if (file_ptr == NULL)
    system_exit (-1);

  list_remove (&file_ptr->elem);

  lock_acquire (&filesys_lock);
  file_close (file_ptr);
  lock_release (&filesys_lock);
}

void mem_scanf(void *src, void *des, int size){
  /* TODO
    Add pointer validity checkup
  */

  char *sr, *de;
  sr = (char *)src;
  de = (char *)des;
  for(int i = 0; i < size; ++i){
    de[i] = sr[i];
  }
}

void 
check_ptr (void *uaddr) {
  if ( !is_user_vaddr(uaddr))
    system_exit (-1);
  if ( !pagedir_get_page (thread_current()->pagedir, uaddr) )
    system_exit (-1);
  if ( !pagedir_get_page (thread_current()->pagedir, uaddr + 3)  )
    system_exit (-1);
}

struct file * find_by_fd(int fd){

  struct list *root = &thread_current()->files_list;
  struct list_elem *ptr;
  struct file *ret = NULL;
  for(ptr = list_begin(root); ptr != list_end(root); ptr = list_next(ptr)){
    struct file *f = list_entry(ptr, struct file, elem);
    if( f->fd == fd){
      ret = f;
      break;
    }
  }

  return ret;
}

void check_buffer(char *file_name, bool check_whole) {
  if(file_name == NULL)
    system_exit (-1);
    // printf("faq %d\n", check_whole);
  check_ptr (file_name);
  if( check_whole ){
    // for(int i = 1; file_name[i]; ++i)
      // check_ptr (file_name + i);
      int i = 1;
      while (1) {
          check_ptr (file_name + i);
          if( file_name[i++] == 0 )
            break;
      }
  }
 
}

void acquire_filesys_lock (void ){
  lock_acquire (&filesys_lock);
}

void release_filesys_lock (void){
  lock_release (&filesys_lock);
}



// pintos -v -k -T 60 --qemu  --filesys-size=2 -p tests/userprog/bad-read -a bad-read -- -q  -f run bad-read
// pintos -v -k -T 60 --qemu  --filesys-size=2 -p tests/userprog/bad-write -a bad-write -- -q  -f run bad-write
// pintos -v -k -T 60 --qemu  --filesys-size=2 -p tests/userprog/bad-write2 -a bad-write2 -- -q  -f run bad-write2
// pintos -v -k -T 60 --qemu  --filesys-size=2 -p tests/userprog/bad-jump -a bad-jump -- -q  -f run bad-jump


// FAIL tests/userprog/exec-once
// FAIL tests/userprog/exec-arg
// FAIL tests/userprog/exec-bound
// pass tests/userprog/exec-bound-2
// FAIL tests/userprog/exec-bound-3
// FAIL tests/userprog/exec-multiple
// pass tests/userprog/exec-missing
// FAIL tests/userprog/exec-bad-ptr
// FAIL tests/userprog/wait-simple
// FAIL tests/userprog/wait-twice
// FAIL tests/userprog/wait-killed
// pass tests/userprog/wait-bad-pid
// FAIL tests/userprog/multi-recurse
// FAIL tests/userprog/multi-child-fd
// FAIL tests/userprog/rox-simple
// FAIL tests/userprog/rox-child
// FAIL tests/userprog/rox-multichild
// pass tests/userprog/bad-read
// pass tests/userprog/bad-write
// pass tests/userprog/bad-read2
// pass tests/userprog/bad-write2
// pass tests/userprog/bad-jump
// pass tests/userprog/bad-jump2
// FAIL tests/userprog/no-vm/multi-oom
// pass tests/filesys/base/lg-create
// pass tests/filesys/base/lg-full
// FAIL tests/filesys/base/lg-random
// pass tests/filesys/base/lg-seq-block
// pass tests/filesys/base/lg-seq-random
// pass tests/filesys/base/sm-create
// pass tests/filesys/base/sm-full
// FAIL tests/filesys/base/sm-random
// pass tests/filesys/base/sm-seq-block
// pass tests/filesys/base/sm-seq-random
// pass tests/filesys/base/syn-read
// FAIL tests/filesys/base/syn-remove
// FAIL tests/filesys/base/syn-write