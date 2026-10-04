#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

#include <xmod/plugin.h>
#include "common.h"

struct dump_all_ctx {
    xmod_core_t *core;
    const xmod_core_api_t *api;
    uint32_t handle;
    const char *outdir;
    int count;
};

static int dump_range(xmod_core_t *core,
                      const xmod_core_api_t *api,
                      uint32_t handle,
                      uint64_t addr,
                      uint64_t len,
                      const char *out)
{
    if (len == 0)
        return 0;

    FILE *fp = fopen(out, "wb");
    if (!fp) {
        perror("fopen out");
        return -1;
    }

    uint8_t *buf = malloc(XPLUG_CHUNK);
    if (!buf) {
        fclose(fp);
        return -1;
    }

    uint64_t remaining = len;
    uint64_t cur = addr;
    int ret = 0;

    while (remaining > 0) {
        size_t want = remaining > XPLUG_CHUNK ? XPLUG_CHUNK : (size_t)remaining;
        size_t done = 0;

        if (api->read(core, handle, cur, buf, want, &done) < 0 || done == 0) {
            ret = -1;
            break;
        }

        if (fwrite(buf, 1, done, fp) != done) {
            ret = -1;
            break;
        }

        cur += done;
        remaining -= done;
    }

    free(buf);
    fclose(fp);

    return ret;
}

static int region_cb(uint64_t start,
                     uint64_t end,
                     const char perms[5],
                     void *ctx)
{
    struct dump_all_ctx *c = ctx;

    if (perms[0] != 'r')
        return 0;

    if (end <= start)
        return 0;

    char fname[PATH_MAX];
    snprintf(fname, sizeof(fname), "%s/%016llx-%016llx.bin",
             c->outdir,
             (unsigned long long)start,
             (unsigned long long)end);

    if (dump_range(c->core, c->api, c->handle, start, end - start, fname) == 0) {
        c->count++;
        c->api->log(c->core, "Dumped region %016llx-%016llx\n",
                    (unsigned long long)start,
                    (unsigned long long)end);
    } else {
        c->api->log(c->core, "Failed region %016llx-%016llx\n",
                    (unsigned long long)start,
                    (unsigned long long)end);
    }

    return 0;
}

static int run_memdump(xmod_core_t *core,
                       const xmod_core_api_t *api,
                       int argc,
                       char **argv)
{
    enum {
        OPT_PID = 1000,
        OPT_PHYSICAL,
        OPT_ADDR,
        OPT_LEN,
        OPT_OUT,
        OPT_OUTDIR,
        OPT_YES,
    };

    static struct option longopts[] = {
        {"pid",      required_argument, NULL, OPT_PID},
        {"physical", no_argument,       NULL, OPT_PHYSICAL},
        {"addr",     required_argument, NULL, OPT_ADDR},
        {"len",      required_argument, NULL, OPT_LEN},
        {"out",      required_argument, NULL, OPT_OUT},
        {"outdir",   required_argument, NULL, OPT_OUTDIR},
        {"yes",      no_argument,       NULL, OPT_YES},
        {NULL,       0,                 NULL, 0},
    };

    pid_t pid = 0;
    bool physical = false;
    bool addr_set = false;
    uint64_t addr = 0;
    uint64_t len = 0;
    const char *out = NULL;
    const char *outdir = NULL;
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
        case OPT_OUT:
            out = optarg;
            break;
        case OPT_OUTDIR:
            outdir = optarg;
            break;
        case OPT_YES:
            assume_yes = true;
            break;
        default:
            return EXIT_FAILURE;
        }
    }

    uint32_t handle = 0;

    if (physical) {
        if (!addr_set || len == 0 || !out) {
            fprintf(stderr, "physical memdump requires --addr, --len, --out\n");
            return EXIT_FAILURE;
        }

        if (!api->confirm(core, "Physical memory read can expose sensitive data.", assume_yes))
            return EXIT_FAILURE;

        if (api->open_physical(core, true, false, &handle) < 0)
            return EXIT_FAILURE;

        int ret = dump_range(core, api, handle, addr, len, out);

        api->close_target(core, handle);

        if (ret == 0)
            printf("Dumped %llu bytes to %s\n",
                   (unsigned long long)len,
                   out);

        return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (pid <= 0) {
        fprintf(stderr, "memdump requires --pid or --physical\n");
        return EXIT_FAILURE;
    }

    if (api->open_process(core, pid, false, &handle) < 0)
        return EXIT_FAILURE;

    int ret = EXIT_SUCCESS;

    if (addr_set && len > 0 && out) {
        if (dump_range(core, api, handle, addr, len, out) == 0) {
            printf("Dumped %llu bytes to %s\n",
                   (unsigned long long)len,
                   out);
        } else {
            ret = EXIT_FAILURE;
        }
    } else if (outdir) {
        if (mkdir(outdir, 0755) < 0 && errno != EEXIST) {
            perror("mkdir outdir");
            api->close_target(core, handle);
            return EXIT_FAILURE;
        }

        struct dump_all_ctx ctx = {
            .core = core,
            .api = api,
            .handle = handle,
            .outdir = outdir,
            .count = 0,
        };

        api->for_each_region(core, pid, region_cb, &ctx);
        printf("Dumped %d regions to %s\n", ctx.count, outdir);
    } else {
        fprintf(stderr,
                "Usage:\n"
                "  memdump --pid PID --addr ADDR --len LEN --out FILE\n"
                "  memdump --pid PID --outdir DIR\n"
                "  memdump --physical --addr ADDR --len LEN --out FILE [--yes]\n");
        ret = EXIT_FAILURE;
    }

    api->close_target(core, handle);
    return ret;
}

const xmod_plugin_info_t *xmod_plugin_info(void)
{
    static const xmod_plugin_info_t info = {
        .abi_version = XMOD_PLUGIN_ABI,
        .name = "memdump",
        .version = "0.1.0",
        .description = "Dump process or physical memory",
    };

    return &info;
}

const xmod_plugin_ops_t *xmod_plugin_ops(void)
{
    static const xmod_plugin_ops_t ops = {
        .run = run_memdump,
    };

    return &ops;
}
