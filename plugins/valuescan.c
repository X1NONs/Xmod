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

struct exact_ctx {
    xmod_core_t *core;
    const xmod_core_api_t *api;
    uint32_t handle;
    xplug_type_t type;
    uint64_t value;
    FILE *fp;
    uint64_t matches;
};

static int scan_exact_region(struct exact_ctx *c,
                             uint64_t start,
                             uint64_t end)
{
    size_t ts = xplug_type_size(c->type);
    if (ts == 0)
        return -1;

    size_t overlap = ts > 1 ? ts - 1 : 0;

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
                uint64_t raw = xplug_raw_from_bytes(buf + i, c->type);

                if (raw == c->value) {
                    fprintf(c->fp, "%llx %llx\n",
                            (unsigned long long)(cur + i),
                            (unsigned long long)raw);
                    c->matches++;
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

static int exact_region_cb(uint64_t start,
                           uint64_t end,
                           const char perms[5],
                           void *ctx)
{
    struct exact_ctx *c = ctx;

    if (perms[0] != 'r')
        return 0;

    scan_exact_region(c, start, end);
    return 0;
}

static int do_compare(xmod_core_t *core,
                      const xmod_core_api_t *api,
                      uint32_t handle,
                      xplug_type_t type,
                      const char *res,
                      const char *out,
                      bool want_changed)
{
    FILE *in = fopen(res, "r");
    if (!in) {
        perror("fopen res");
        return -1;
    }

    FILE *fp = stdout;
    if (out) {
        fp = fopen(out, "w");
        if (!fp) {
            perror("fopen out");
            fclose(in);
            return -1;
        }
    }

    size_t ts = xplug_type_size(type);
    char line[256];
    uint64_t count = 0;

    while (fgets(line, sizeof(line), in)) {
        unsigned long long addr = 0;
        unsigned long long old = 0;

        if (sscanf(line, "%llx %llx", &addr, &old) != 2)
            continue;

        uint8_t buf[8];
        size_t done = 0;

        if (api->read(core, handle, (uint64_t)addr, buf, ts, &done) < 0 || done != ts)
            continue;

        uint64_t raw = xplug_raw_from_bytes(buf, type);
        bool changed = (raw != (uint64_t)old);

        if (changed == want_changed) {
            fprintf(fp, "%llx %llx\n", addr, (unsigned long long)raw);
            count++;
        }
    }

    if (fp != stdout)
        fclose(fp);

    fclose(in);

    fprintf(stderr, "Found %llu matches\n", (unsigned long long)count);
    return 0;
}

static int run_valuescan(xmod_core_t *core,
                         const xmod_core_api_t *api,
                         int argc,
                         char **argv)
{
    enum {
        OPT_PID = 1000,
        OPT_TYPE,
        OPT_EXACT,
        OPT_CHANGED,
        OPT_UNCHANGED,
        OPT_RES,
        OPT_OUT,
    };

    static struct option longopts[] = {
        {"pid",       required_argument, NULL, OPT_PID},
        {"type",      required_argument, NULL, OPT_TYPE},
        {"exact",     required_argument, NULL, OPT_EXACT},
        {"changed",   no_argument,       NULL, OPT_CHANGED},
        {"unchanged", no_argument,       NULL, OPT_UNCHANGED},
        {"res",       required_argument, NULL, OPT_RES},
        {"out",       required_argument, NULL, OPT_OUT},
        {NULL,        0,                 NULL, 0},
    };

    pid_t pid = 0;
    const char *type_str = NULL;
    const char *exact_str = NULL;
    const char *res = NULL;
    const char *out = NULL;
    bool changed = false;
    bool unchanged = false;

    optind = 0;

    int c;
    while ((c = getopt_long(argc, argv, "", longopts, NULL)) != -1) {
        switch (c) {
        case OPT_PID:
            pid = (pid_t)xplug_parse_u64(optarg);
            break;
        case OPT_TYPE:
            type_str = optarg;
            break;
        case OPT_EXACT:
            exact_str = optarg;
            break;
        case OPT_CHANGED:
            changed = true;
            break;
        case OPT_UNCHANGED:
            unchanged = true;
            break;
        case OPT_RES:
            res = optarg;
            break;
        case OPT_OUT:
            out = optarg;
            break;
        default:
            return EXIT_FAILURE;
        }
    }

    if (pid <= 0 || !type_str) {
        fprintf(stderr,
                "Usage:\n"
                "  valuescan --pid PID --type TYPE --exact VALUE [--out FILE]\n"
                "  valuescan --pid PID --type TYPE --res FILE --changed|--unchanged [--out FILE]\n");
        return EXIT_FAILURE;
    }

    xplug_type_t type;
    if (xplug_parse_type(type_str, &type) < 0) {
        fprintf(stderr, "Bad type. Use u8, u16, u32, u64, float\n");
        return EXIT_FAILURE;
    }

    uint32_t handle = 0;
    if (api->open_process(core, pid, false, &handle) < 0)
        return EXIT_FAILURE;

    int ret = EXIT_SUCCESS;

    if (exact_str) {
        uint64_t value = 0;

        if (xplug_parse_value(exact_str, type, &value) < 0) {
            fprintf(stderr, "Bad value\n");
            api->close_target(core, handle);
            return EXIT_FAILURE;
        }

        FILE *fp = stdout;
        if (out) {
            fp = fopen(out, "w");
            if (!fp) {
                perror("fopen out");
                api->close_target(core, handle);
                return EXIT_FAILURE;
            }
        }

        struct exact_ctx ctx = {
            .core = core,
            .api = api,
            .handle = handle,
            .type = type,
            .value = value,
            .fp = fp,
            .matches = 0,
        };

        api->for_each_region(core, pid, exact_region_cb, &ctx);

        fprintf(stderr, "Found %llu matches\n",
                (unsigned long long)ctx.matches);

        if (fp != stdout)
            fclose(fp);

    } else if (changed || unchanged) {
        if (!res) {
            fprintf(stderr, "--changed/--unchanged requires --res FILE\n");
            api->close_target(core, handle);
            return EXIT_FAILURE;
        }

        if (changed && unchanged) {
            fprintf(stderr, "Use only one of --changed or --unchanged\n");
            api->close_target(core, handle);
            return EXIT_FAILURE;
        }

        if (do_compare(core, api, handle, type, res, out, changed) < 0)
            ret = EXIT_FAILURE;

    } else {
        fprintf(stderr, "Need --exact or --changed/--unchanged\n");
        ret = EXIT_FAILURE;
    }

    api->close_target(core, handle);
    return ret;
}

const xmod_plugin_info_t *xmod_plugin_info(void)
{
    static const xmod_plugin_info_t info = {
        .abi_version = XMOD_PLUGIN_ABI,
        .name = "valuescan",
        .version = "0.1.0",
        .description = "Scan process memory for typed values",
    };

    return &info;
}

const xmod_plugin_ops_t *xmod_plugin_ops(void)
{
    static const xmod_plugin_ops_t ops = {
        .run = run_valuescan,
    };

    return &ops;
}
