#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void exit_with_code (int status);

struct file *fd_to_file (int fd);
int file_to_fd (struct file *file);
void remove_fd (int fd);

#endif /* userprog/syscall.h */
