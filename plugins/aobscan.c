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

struct aob_ctx {
    xmod_core_t *core;
    const xmod_core_api_t *api;
    uint32_t handle;
    const xplug_pattern_t *pat;
    FILE *fp;
    uint64_t count;
};

static int scan_range_aob(struct aob_ctx *c,
                          uint64_t start,
                          uint64_t end)
{
    size_t plen = c->pat->len;

    if (plen == 0 || plen > XPLUG_CHUNK)
        return -1;

    size_t overlap = plen > 1 ? plen - 1 : 0;

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

        if (done >= first + plen) {
            for (size_t i = first; i + plen <= done; i++) {
                if (xplug_pattern_match(buf + i, c->pat)) {
                    fprintf(c->fp, "%llx\n",
                            (unsigned long long)(cur + i));
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

static int aob_region_cb(uint64_t start,
                         uint64_t end,
                         const char perms[5],
                         void *ctx)
{
    struct aob_ctx *c = ctx;

    if (perms[0] != 'r')
        return 0;

    scan_range_aob(c, start, end);
    return 0;
}

static int run_aobscan(xmod_core_t *core,
                       const xmod_core_api_t *api,
                       int argc,
                       char **argv)
{
    enum {
        OPT_PID = 1000,
        OPT_PHYSICAL,
        OPT_ADDR,
        OPT_LEN,
        OPT_PATTERN,
        OPT_OUT,
        OPT_YES,
    };

    static struct option longopts[] = {
        {"pid",      required_argument, NULL, OPT_PID},
        {"physical", no_argument,       NULL, OPT_PHYSICAL},
        {"addr",     required_argument, NULL, OPT_ADDR},
        {"len",      required_argument, NULL, OPT_LEN},
        {"pattern",  required_argument, NULL, OPT_PATTERN},
        {"out",      required_argument, NULL, OPT_OUT},
        {"yes",      no_argument,       NULL, OPT_YES},
        {NULL,       0,                 NULL, 0},
    };

    pid_t pid = 0;
    bool physical = false;
    bool addr_set = false;
    uint64_t addr = 0;
    uint64_t len = 0;
    const char *pattern_str = NULL;
    const char *out = NULL;
    bool assume_yes = false;

    optind = 0;

    int c;
    while ((c = getopt_long(argc, argv, "", longopts, NULL)) != -1) {
        switch (c) {
        case OPT_PID:
            pid = (pid_t)xplug_parse_u64(optarg);
            break;
        case OPT_PHYSICAL:
            physical = true;
            break;
        case OPT_ADDR:
            addr = xplug_parse_u64(optarg);
            addr_set = true;
            break;
        case OPT_LEN:
            len = xplug_parse_u64(optarg);
            break;
        case OPT_PATTERN:
            pattern_str = optarg;
            break;
        case OPT_OUT:
            out = optarg;
            break;
        case OPT_YES:
            assume_yes = true;
            break;
        default:
            return EXIT_FAILURE;
        }
    }

    if (!pattern_str) {
        fprintf(stderr, "aobscan requires --pattern\n");
        return EXIT_FAILURE;
    }

    xplug_pattern_t pat;
    if (xplug_parse_pattern(pattern_str, &pat) < 0) {
        fprintf(stderr, "Invalid pattern\n");
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
    int ret = EXIT_SUCCESS;

    struct aob_ctx ctx = {
        .core = core,
        .api = api,
        .handle = 0,
        .pat = &pat,
        .fp = fp,
        .count = 0,
    };

    if (physical) {
        if (!addr_set || len == 0) {
            fprintf(stderr, "physical aobscan requires --addr and --len\n");
            ret = EXIT_FAILURE;
            goto close_out;
        }

        if (!api->confirm(core, "Physical memory read can expose sensitive data.", assume_yes)) {
            ret = EXIT_FAILURE;
            goto close_out;
        }

        if (api->open_physical(core, true, false, &handle) < 0) {
            ret = EXIT_FAILURE;
            goto close_out;
        }

        ctx.handle = handle;
        scan_range_aob(&ctx, addr, addr + len);

        api->close_target(core, handle);
    } else {
        if (pid <= 0) {
            fprintf(stderr, "aobscan requires --pid or --physical\n");
            ret = EXIT_FAILURE;
            goto close_out;
        }

        if (api->open_process(core, pid, false, &handle) < 0) {
            ret = EXIT_FAILURE;
            goto close_out;
        }

        ctx.handle = handle;
        api->for_each_region(core, pid, aob_region_cb, &ctx);

        api->close_target(core, handle);
    }

    fprintf(stderr, "Found %llu matches\n", (unsigned long long)ctx.count);

close_out:
    if (fp != stdout)
        fclose(fp);

    return ret;
}

const xmod_plugin_info_t *xmod_plugin_info(void)
{
    static const xmod_plugin_info_t info = {
        .abi_version = XMOD_PLUGIN_ABI,
        .name = "aobscan",
        .version = "0.1.0",
        .description = "Scan memory for byte patterns",
    };

    return &info;
}

const xmod_plugin_ops_t *xmod_plugin_ops(void)
{
    static const xmod_plugin_ops_t ops = {
        .run = run_aobscan,
    };

    return &ops;
}
