/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/ipc.h>
#include <core/vfs.h>
#include <core/mm.h>

extern void *memcpy(void *, const void *, size_t);

int pipe_create(struct pipe **pp) {
    struct pipe *p = (struct pipe *)kmalloc(sizeof(struct pipe));
    if (!p) return -ENOMEM;
    p->read_pos    = 0;
    p->write_pos   = 0;
    p->count       = 0;
    p->read_closed  = 0;
    p->write_closed = 0;
    p->readers = 1;
    p->writers = 1;
    *pp = p;
    return 0;
}

void pipe_destroy(struct pipe *p) {
    if (p) kfree(p);
}

ssize_t pipe_read(struct pipe *p, void *buf, size_t n) {
    if (!p) return -EBADF;
    if (p->count == 0) {
        if (p->write_closed) return 0;  /* EOF */
        return -EAGAIN;
    }
    size_t i = 0;
    char *out = (char *)buf;
    while (i < n && p->count > 0) {
        out[i++] = (char)p->buf[p->read_pos];
        p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
        p->count--;
    }
    return (ssize_t)i;
}

ssize_t pipe_write(struct pipe *p, const void *buf, size_t n) {
    if (!p) return -EBADF;
    if (p->read_closed) return -EPIPE;
    size_t i = 0;
    const char *in = (const char *)buf;
    while (i < n && p->count < PIPE_BUF_SIZE) {
        p->buf[p->write_pos] = (uint8_t)in[i++];
        p->write_pos = (p->write_pos + 1) % PIPE_BUF_SIZE;
        p->count++;
    }
    return (ssize_t)i;
}

int sys_pipe(int fds[2]) {
    struct pipe *p = NULL;
    int r = pipe_create(&p);
    if (r < 0) return r;

    int rfd = vfs_alloc_fd_pipe(p, O_RDONLY);
    if (rfd < 0) { pipe_destroy(p); return rfd; }

    int wfd = vfs_alloc_fd_pipe(p, O_WRONLY);
    if (wfd < 0) { vfs_close(rfd); pipe_destroy(p); return wfd; }

    fds[0] = rfd;
    fds[1] = wfd;
    return 0;
}
