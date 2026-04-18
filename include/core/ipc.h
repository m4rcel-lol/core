/* CORE Kernel — (c) CORE Project, MIT License */
#ifndef CORE_IPC_H
#define CORE_IPC_H

#include <core/types.h>

#define PIPE_BUF_SIZE 4096

struct pipe {
    uint8_t  buf[PIPE_BUF_SIZE];
    size_t   read_pos;
    size_t   write_pos;
    size_t   count;
    int      read_closed;
    int      write_closed;
    int      readers;
    int      writers;
};

int  pipe_create(struct pipe **pp);
void pipe_destroy(struct pipe *p);
ssize_t pipe_read(struct pipe *p, void *buf, size_t n);
ssize_t pipe_write(struct pipe *p, const void *buf, size_t n);
int  sys_pipe(int fds[2]);

#endif /* CORE_IPC_H */
