#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/* System call numbers. */
enum {
    /* System calls for user programs implementation. */
    SYS_HALT,       /* Halt the operating system. */
    SYS_EXIT,       /* Terminate the current user program. */
    SYS_EXEC,       /* Run executable specified in command line. */
    SYS_WAIT,       /* Wait for child process to exit/die. */
    SYS_CREATE,     /* Create a new file. */
    SYS_REMOVE,     /* Delete a file. */
    SYS_OPEN,       /* Open a file. */
    SYS_FILESIZE,   /* Get size of file. */
    SYS_READ,       /* Read from a file. */
    SYS_WRITE,      /* Write to a file. */
    SYS_SEEK,       /* Change position in the file. */
    SYS_TELL,       /* Get current position in the file. */
    SYS_CLOSE       /* Close the file. */
};

void syscall_init (void);

#endif /* userprog/syscall.h */
