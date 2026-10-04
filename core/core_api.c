#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "backend.h"
#include "core_api.h"

static void api_log(xmod_core_t *core, const char *fmt, ...)
{
    (void)core;

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static int api_open_process(xmod_core_t *core,
                            pid_t pid,
                            bool write,
                            uint32_t *handle)
{
    (void)core;
    return xmod_backend_open_process(pid, write, handle);
}

static int api_open_physical(xmod_core_t *core,
                             bool read,
                             bool write,
                             uint32_t *handle)
{
    (void)core;
    return xmod_backend_open_physical(read, write, handle);
}

static int api_read(xmod_core_t *core,
                    uint32_t handle,
                    uint64_t addr,
                    void *buf,
                    size_t len,
                    size_t *done)
{
    (void)core;
    return xmod_backend_read(handle, addr, buf, len, done);
}

static int api_write(xmod_core_t *core,
                     uint32_t handle,
                     uint64_t addr,
                     const void *buf,
                     size_t len,
                     size_t *done)
{
    (void)core;
    return xmod_backend_write(handle, addr, buf, len, done);
}

static int api_close_target(xmod_core_t *core,
                            uint32_t handle)
{
    (void)core;
    return xmod_backend_close_target(handle);
}

static int api_for_each_region(xmod_core_t *core,
                               pid_t pid,
                               xmod_region_cb cb,
                               void *ctx)
{
    (void)core;

    if (!cb)
        return -1;

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", (int)pid);

    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    char line[1024];

    while (fgets(line, sizeof(line), f)) {
        unsigned long long start = 0;
        unsigned long long end = 0;
        char perms[5] = {0};

        if (sscanf(line, "%llx-%llx %4s", &start, &end, perms) == 3) {
            if (cb((uint64_t)start, (uint64_t)end, perms, ctx))
                break;
        }
    }

    fclose(f);
    return 0;
}

static bool api_confirm(xmod_core_t *core,
                        const char *message,
                        bool assume_yes)
{
    if (assume_yes)
        return true;

    if (!isatty(STDIN_FILENO)) {
        api_log(core, "%s\nRe-run with --yes to confirm.\n", message);
        return false;
    }

    fprintf(stderr, "%s\nType YES to continue: ", message);
    fflush(stderr);

    char line[32] = {0};
    if (!fgets(line, sizeof(line), stdin))
        return false;

    line[strcspn(line, "\n")] = '\0';

    return strcmp(line, "YES") == 0;
}

const xmod_core_api_t xmod_core_api = {
    .open_process   = api_open_process,
    .open_physical  = api_open_physical,
    .read           = api_read,
    .write          = api_write,
    .close_target   = api_close_target,
    .for_each_region = api_for_each_region,
    .confirm        = api_confirm,
    .log            = api_log,
};
