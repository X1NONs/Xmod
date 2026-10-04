#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>

#include <xmod/plugin.h>
#include "common.h"

struct ptr_ctx {
    xmod_core_t *core;
    const xmod_core_api_t *api;
    uint32_t handle;
    uint64_t target;
    uint64_t max_offset;
    uint64_t align;
    FILE *fp;
    uint64_t count;
};

static int scan_ptr_region(struct ptr_ctx *c,
                           uint64_t start,
                           uint64_t end)
{
    size_t ts = sizeof(uint64_t);
    size_t overlap = ts - 1;

    uint8_t *buf = malloc(XPLUG_CHUNK);
    if (!buf)
        return -1;

    uint64_t cur = start;

    while (cur < end) {
        size_t want = XPLUG_CHUNK;
        if (end - cur < want)
            want = (size_t)(end - cur);

        size_t done = 0;
        if (c->api->read(c->core, c->handle, cur, buf, want, &done) < 0 || done == 0)
            break;

        size_t first = (cur == start) ? 0 : overlap;

        if (done >= first + ts) {
            for (size_t i = first; i + ts <= done; i++) {
                uint64_t ptr_addr = cur + i;

                if (c->align > 1 && (ptr_addr % c->align) != 0)
                    continue;

                uint64_t value;
                memcpy(&value, buf + i, sizeof(value));

                uint64_t diff;
                if (value > c->target)
                    diff = value - c->target;
                else
                    diff = c->target - value;

                if (diff <= c->max_offset) {
                    long long offset = (long long)((int64_t)c->target - (int64_t)value);

                    fprintf(c->fp, "%016llx %016llx %lld\n",
                            (unsigned long long)ptr_addr,
                            (unsigned long long)value,
                            offset);

                    c->count++;
                }
            }
        }

        if (done <= overlap)
            break;

        cur += done - overlap;
    }

    free(buf);
    return 0;
}

static int ptr_region_cb(uint64_t start,
                         uint64_t end,
                         const char perms[5],
                         void *ctx)
{
    struct ptr_ctx *c = ctx;

    if (perms[0] != 'r')
        return 0;

    scan_ptr_region(c, start, end);
    return 0;
}

static int run_ptrscan(xmod_core_t *core,
                       const xmod_core_api_t *api,
                       int argc,
                       char **argv)
{
    enum {
        OPT_PID = 1000,
        OPT_TARGET,
        OPT_MAX_OFFSET,
        OPT_ALIGN,
        OPT_OUT,
    };

    static struct option longopts[] = {
        {"pid",        required_argument, NULL, OPT_PID},
        {"target",     required_argument, NULL, OPT_TARGET},
        {"max-offset", required_argument, NULL, OPT_MAX_OFFSET},
        {"align",      required_argument, NULL, OPT_ALIGN},
        {"out",        required_argument, NULL, OPT_OUT},
        {NULL,         0,                 NULL, 0},
    };

    pid_t pid = 0;
    bool target_set = false;
    uint64_t target = 0;
    uint64_t max_offset = 4096;
    uint64_t align = 8;
    const char *out = NULL;

    optind = 0;

    int c;
    while ((c = getopt_long(argc, argv, "", longopts, NULL)) != -1) {
        switch (c) {
        case OPT_PID:
            pid = (pid_t)xplug_parse_u64(optarg);
            break;
        case OPT_TARGET:
            target = xplug_parse_u64(optarg);
            target_set = true;
            break;
        case OPT_MAX_OFFSET:
            max_offset = xplug_parse_u64(optarg);
            break;
        case OPT_ALIGN:
            align = xplug_parse_u64(optarg);
            break;
        case OPT_OUT:
            out = optarg;
            break;
        default:
            return EXIT_FAILURE;
        }
    }

    if (pid <= 0 || !target_set) {
        fprintf(stderr, "ptrscan requires --pid and --target\n");
        return EXIT_FAILURE;
    }

    if (align == 0) {
        fprintf(stderr, "align cannot be zero\n");
        return EXIT_FAILURE;
    }

    FILE *fp = stdout;
    if (out) {
        fp = fopen(out, "w");
        if (!fp) {
            perror("fopen out");
            return EXIT_FAILURE;
        }
    }

    uint32_t handle = 0;
    if (api->open_process(core, pid, false, &handle) < 0) {
        if (fp != stdout)
            fclose(fp);
        return EXIT_FAILURE;
    }

    struct ptr_ctx ctx = {
        .core = core,
        .api = api,
        .handle = handle,
        .target = target,
        .max_offset = max_offset,
        .align = align,
        .fp = fp,
        .count = 0,
    };

    api->for_each_region(core, pid, ptr_region_cb, &ctx);

    api->close_target(core, handle);

    fprintf(stderr, "Found %llu pointer candidates\n",
            (unsigned long long)ctx.count);

    if (fp != stdout)
        fclose(fp);

    return EXIT_SUCCESS;
}

const xmod_plugin_info_t *xmod_plugin_info(void)
{
    static const xmod_plugin_info_t info = {
        .abi_version = XMOD_PLUGIN_ABI,
        .name = "ptrscan",
        .version = "0.1.0",
        .description = "Scan for pointer candidates",
    };

    return &info;
}

const xmod_plugin_ops_t *xmod_plugin_ops(void)
{
    static const xmod_plugin_ops_t ops = {
        .run = run_ptrscan,
    };

    return &ops;
}
