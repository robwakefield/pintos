#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H



void syscall_init (void);
void exit_with_code (int status);
void file_init(void);

struct lock filesys_lock;

#endif /* userprog/syscall.h */
