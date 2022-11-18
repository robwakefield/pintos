#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H


#define FD_SIZE 32

void syscall_init (void);
void exit_with_code (int status);
void file_init(void);
struct file* fd_to_file(int);
struct lock filesys_lock;
#endif /* userprog/syscall.h */
