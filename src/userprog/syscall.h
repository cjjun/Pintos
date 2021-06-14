#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

void acquire_filesys_lock (void);
void release_filesys_lock (void);
void system_exit (int exit_code);

#endif /* userprog/syscall.h */
