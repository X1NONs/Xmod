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

static int make_snapshot(xmod_core_t *core,
                         const xmod_core_api_t *api,
                         pid_t pid,
                         const char *input,
                         const char *snap,
                         xplug_type_t type)
{
    FILE *in = fopen(input, "r");
    if (!in) {
        perror("fopen input");
        return -1;
    }

    FILE *out = fopen(snap, "w");
    if (!out) {
        perror("fopen snapshot");
        fclose(in);
        return -1;
    }

    uint32_t handle = 0;
    if (api->open_process(core, pid, false, &handle) < 0) {
        fclose(in);
        fclose(out);
        return -1;
    }

    size_t ts = xplug_type_size(type);
    char line[256];
    uint64_t count = 0;

    while (fgets(line, sizeof(line), in)) {
        unsigned long long addr = 0;

        if (sscanf(line, "%llx", &addr) != 1)
            continue;

        uint8_t buf[8] = {0};
        size_t done = 0;
        uint64_t raw = 0;

        if (api->read(core, handle, (uint64_t)addr, buf, ts, &done) == 0 && done == ts)
            raw = xplug_raw_from_bytes(buf, type);

        fprintf(out, "%llx %llx\n", addr, (unsigned long long)raw);
        count++;
    }

    api->close_target(core, handle);
    fclose(in);
    fclose(out);

    fprintf(stderr, "Snapshot saved: %llu addresses\n",
            (unsigned long long)count);

    return 0;
}

static int diff_snapshots(const char *snap1,
                          const char *snap2,
                          const char *out,
                          bool want_changed)
{
    FILE *f1 = fopen(snap1, "r");
    if (!f1) {
        perror("fopen snap1");
        return -1;
    }

    FILE *f2 = fopen(snap2, "r");
    if (!f2) {
        perror("fopen snap2");
        fclose(f1);
        return -1;
    }

    FILE *fp = stdout;
    if (out) {
        fp = fopen(out, "w");
        if (!fp) {
            perror("fopen out");
            fclose(f1);
            fclose(f2);
            return -1;
        }
    }

    char line1[256];
    char line2[256];
    uint64_t count = 0;

    while (fgets(line1, sizeof(line1), f1) && fgets(line2, sizeof(line2), f2)) {
        unsigned long long a1 = 0, v1 = 0;
        unsigned long long a2 = 0, v2 = 0;

        if (sscanf(line1, "%llx %llx", &a1, &v1) != 2)
            continue;

        if (sscanf(line2, "%llx %llx", &a2, &v2) != 2)
            continue;

        if (a1 != a2) {
            fprintf(stderr, "Snapshot address mismatch at %llx vs %llx\n",
                    a1, a2);
            break;
        }

        bool changed = (v1 != v2);

        if (changed == want_changed) {
            fprintf(fp, "%llx %llx\n", a1, v2);
            count++;
        }
    }

    if (fp != stdout)
        fclose(fp);

    fclose(f1);
    fclose(f2);

    fprintf(stderr, "Found %llu matches\n", (unsigned long long)count);
    return 0;
}

static int run_memwatch(xmod_core_t *core,
                        const xmod_core_api_t *api,
                        int argc,
                        char **argv)
{
    enum {
        OPT_PID = 1000,
        OPT_INPUT,
        OPT_TYPE,
        OPT_SNAPSHOT,
        OPT_SNAP1,
        OPT_SNAP2,
        OPT_DIFF,
        OPT_OUT,
    };

    static struct option longopts[] = {
        {"pid",      required_argument, NULL, OPT_PID},
        {"input",    required_argument, NULL, OPT_INPUT},
        {"type",     required_argument, NULL, OPT_TYPE},
        {"snapshot", required_argument, NULL, OPT_SNAPSHOT},
        {"snap1",    required_argument, NULL, OPT_SNAP1},
        {"snap2",    required_argument, NULL, OPT_SNAP2},
        {"diff",     required_argument, NULL, OPT_DIFF},
        {"out",      required_argument, NULL, OPT_OUT},
        {NULL,       0,                 NULL, 0},
    };

    pid_t pid = 0;
    const char *input = NULL;
    const char *type_str = NULL;
    const char *snapshot = NULL;
    const char *snap1 = NULL;
    const char *snap2 = NULL;
    const char *diff = NULL;
    const char *out = NULL;

    optind = 0;

    int c;
    while ((c = getopt_long(argc, argv, "", longopts, NULL)) != -1) {
        switch (c) {
        case OPT_PID:
            pid = (pid_t)xplug_parse_u64(optarg);
            break;
        case OPT_INPUT:
            input = optarg;
            break;
        case OPT_TYPE:
            type_str = optarg;
            break;
        case OPT_SNAPSHOT:
            snapshot = optarg;
            break;
        case OPT_SNAP1:
            snap1 = optarg;
            break;
        case OPT_SNAP2:
            snap2 = optarg;
            break;
        case OPT_DIFF:
            diff = optarg;
            break;
        case OPT_OUT:
            out = optarg;
            break;
        default:
            return EXIT_FAILURE;
        }
    }

    if (snapshot) {
        if (pid <= 0 || !input || !type_str) {
            fprintf(stderr,
                    "snapshot mode requires --pid, --input, --type, --snapshot\n");
            return EXIT_FAILURE;
        }

        xplug_type_t type;
        if (xplug_parse_type(type_str, &type) < 0) {
            fprintf(stderr, "Bad type\n");
            return EXIT_FAILURE;
        }

        return make_snapshot(core, api, pid, input, snapshot, type) == 0
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }

    if (diff) {
        if (!snap1 || !snap2) {
            fprintf(stderr, "diff mode requires --snap1 and --snap2\n");
            return EXIT_FAILURE;
        }

        bool want_changed;

        if (!strcmp(diff, "changed"))
            want_changed = true;
        else if (!strcmp(diff, "unchanged"))
            want_changed = false;
        else {
            fprintf(stderr, "--diff must be changed or unchanged\n");
            return EXIT_FAILURE;
        }

        return diff_snapshots(snap1, snap2, out, want_changed) == 0
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }

    fprintf(stderr,
            "Usage:\n"
            "  memwatch --pid PID --input FILE --type TYPE --snapshot OUT\n"
            "  memwatch --snap1 FILE --snap2 FILE --diff changed|unchanged [--out FILE]\n");

    return EXIT_FAILURE;
}

const xmod_plugin_info_t *xmod_plugin_info(void)
{
    static const xmod_plugin_info_t info = {
        .abi_version = XMOD_PLUGIN_ABI,
        .name = "memwatch",
        .version = "0.1.0",
        .description = "Snapshot and compare memory values",
    };

    return &info;
}

const xmod_plugin_ops_t *xmod_plugin_ops(void)
{
    static const xmod_plugin_ops_t ops = {
        .run = run_memwatch,
    };

    return &ops;
}
