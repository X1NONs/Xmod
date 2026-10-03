#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <xmod/uapi.h>
#include "backend.h"

static int xmod_fd = -1;

int xmod_backend_open(void)
{
    struct xmod_handshake h = {
        .version = XMOD_API_VERSION,
    };

    xmod_fd = open("/dev/xmod", O_RDWR | O_CLOEXEC);
    if (xmod_fd < 0) {
        perror("open /dev/xmod");
        return -1;
    }

    if (ioctl(xmod_fd, XMOD_CMD_HANDSHAKE, &h) < 0) {
        perror("ioctl XMOD_CMD_HANDSHAKE");
        close(xmod_fd);
        xmod_fd = -1;
        return -1;
    }

    return 0;
}

void xmod_backend_close(void)
{
    if (xmod_fd >= 0) {
        close(xmod_fd);
        xmod_fd = -1;
    }
}

int xmod_backend_query_caps(uint64_t *caps)
{
    struct xmod_query_caps q = {
        .version = XMOD_API_VERSION,
    };

    if (xmod_fd < 0)
        return -1;

    if (ioctl(xmod_fd, XMOD_CMD_QUERY_CAPS, &q) < 0) {
        perror("ioctl XMOD_CMD_QUERY_CAPS");
        return -1;
    }

    *caps = q.global_caps;
    return 0;
}

int xmod_backend_open_process(pid_t pid, bool write, uint32_t *handle)
{
    struct xmod_open_target req = {
        .version = XMOD_API_VERSION,
        .type = XMOD_TARGET_PROCESS,
        .id = (uint32_t)pid,
        .flags = XMOD_OPEN_FLAG_READ | (write ? XMOD_OPEN_FLAG_WRITE : 0),
        .requested_caps = XMOD_CAP_PROCESS_READ |
                          (write ? XMOD_CAP_PROCESS_WRITE : 0),
    };

    if (xmod_fd < 0)
        return -1;

    if (ioctl(xmod_fd, XMOD_CMD_OPEN_TARGET, &req) < 0) {
        perror("ioctl XMOD_CMD_OPEN_TARGET process");
        return -1;
    }

    *handle = req.out_handle;
    return 0;
}

int xmod_backend_open_physical(bool read, bool write, uint32_t *handle)
{
    struct xmod_open_target req = {
        .version = XMOD_API_VERSION,
        .type = XMOD_TARGET_PHYSICAL,
        .id = 0,
        .flags = (read ? XMOD_OPEN_FLAG_READ : 0) |
                 (write ? XMOD_OPEN_FLAG_WRITE : 0),
        .requested_caps = (read ? XMOD_CAP_PHYSICAL_READ : 0) |
                          (write ? XMOD_CAP_PHYSICAL_WRITE : 0),
    };

    if (xmod_fd < 0)
        return -1;

    if (!(req.flags & (XMOD_OPEN_FLAG_READ | XMOD_OPEN_FLAG_WRITE)))
        return -1;

    if (ioctl(xmod_fd, XMOD_CMD_OPEN_TARGET, &req) < 0) {
        perror("ioctl XMOD_CMD_OPEN_TARGET physical");
        return -1;
    }

    *handle = req.out_handle;
    return 0;
}

int xmod_backend_read(uint32_t handle, uint64_t addr, void *buf, size_t len, size_t *done)
{
    struct xmod_rw_req req = {
        .version = XMOD_API_VERSION,
        .handle = handle,
        .remote_addr = addr,
        .user_ptr = (uint64_t)(uintptr_t)buf,
        .length = len,
        .bytes_done = 0,
    };

    if (xmod_fd < 0)
        return -1;

    if (ioctl(xmod_fd, XMOD_CMD_READ, &req) < 0) {
        perror("ioctl XMOD_CMD_READ");
        return -1;
    }

    if (done)
        *done = req.bytes_done;

    return 0;
}

int xmod_backend_write(uint32_t handle, uint64_t addr, const void *buf, size_t len, size_t *done)
{
    struct xmod_rw_req req = {
        .version = XMOD_API_VERSION,
        .handle = handle,
        .remote_addr = addr,
        .user_ptr = (uint64_t)(uintptr_t)buf,
        .length = len,
        .bytes_done = 0,
    };

    if (xmod_fd < 0)
        return -1;

    if (ioctl(xmod_fd, XMOD_CMD_WRITE, &req) < 0) {
        perror("ioctl XMOD_CMD_WRITE");
        return -1;
    }

    if (done)
        *done = req.bytes_done;

    return 0;
}

int xmod_backend_close_target(uint32_t handle)
{
    if (xmod_fd < 0)
        return -1;

    if (ioctl(xmod_fd, XMOD_CMD_CLOSE_TARGET, &handle) < 0) {
        perror("ioctl XMOD_CMD_CLOSE_TARGET");
        return -1;
    }

    return 0;
}
