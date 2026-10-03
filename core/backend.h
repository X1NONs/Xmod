#ifndef XMOD_BACKEND_H
#define XMOD_BACKEND_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

int xmod_backend_open(void);
void xmod_backend_close(void);

int xmod_backend_query_caps(uint64_t *caps);

int xmod_backend_open_process(pid_t pid, bool write, uint32_t *handle);
int xmod_backend_open_physical(bool read, bool write, uint32_t *handle);

int xmod_backend_read(uint32_t handle, uint64_t addr, void *buf, size_t len, size_t *done);
int xmod_backend_write(uint32_t handle, uint64_t addr, const void *buf, size_t len, size_t *done);

int xmod_backend_close_target(uint32_t handle);

#endif
