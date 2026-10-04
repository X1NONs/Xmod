#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <xmod/uapi.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <getopt.h>
#include <sys/stat.h>

#include "backend.h"

#define CHUNK_SIZE (1024 * 1024)
#define MAX_VALUE  (64 * 1024)

static void usage(void)
{
    fprintf(stderr,
        "Usage:\n"
        "  xmod-core caps\n"
        "  xmod-core read  --pid <pid> --addr <addr> --len <len> [--file out]\n"
        "  xmod-core read  --physical --addr <addr> --len <len> [--file out] [--yes]\n"
        "  xmod-core write --pid <pid> --addr <addr> [--u8 v|--u16 v|--u32 v|--u64 v|--float v|--string s|--bytes hex]\n"
        "  xmod-core write --physical --addr <addr> [--u8 v|--u16 v|--u32 v|--u64 v|--float v|--string s|--bytes hex] [--yes]\n"
    );
}

static uint64_t parse_u64(const char *s)
{
    char *end = NULL;
    errno = 0;

    unsigned long long v = strtoull(s, &end, 0);
    if (errno != 0 || end == NULL || *end != '\0') {
        fprintf(stderr, "Invalid number: %s\n", s);
        exit(EXIT_FAILURE);
    }

    return (uint64_t)v;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int hex2bin(const char *s, uint8_t *out, size_t max, size_t *outlen)
{
    size_t o = 0;
    int nibble = -1;

    for (size_t i = 0; s[i] != '\0'; i++) {
        int c = (unsigned char)s[i];

        if (c == ':' || c == ' ' || c == ',' || c == '\n' || c == '\r' || c == '\t')
            continue;

        if ((c == 'x' || c == 'X') && i > 0 && s[i - 1] == '0')
            continue;

        int v = hexval(c);
        if (v < 0)
            return -1;

        if (nibble < 0) {
            nibble = v;
        } else {
            if (o >= max)
                return -1;
            out[o++] = (uint8_t)((nibble << 4) | v);
            nibble = -1;
        }
    }

    if (nibble >= 0)
        return -1;

    *outlen = o;
    return 0;
}

static void hexdump(uint64_t base, const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i += 16) {
        printf("%016llx  ", (unsigned long long)(base + i));

        for (size_t j = 0; j < 16; j++) {
            if (i + j < len)
                printf("%02x ", buf[i + j]);
            else
                printf("   ");

            if (j == 7)
                printf(" ");
        }

        printf(" |");

        for (size_t j = 0; j < 16 && i + j < len; j++) {
            uint8_t c = buf[i + j];
            printf("%c", isprint(c) ? c : '.');
        }

        printf("|\n");
    }
}

static bool confirm(const char *message, bool assume_yes)
{
    if (assume_yes)
        return true;

    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "%s\nRe-run with --yes to confirm.\n", message);
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

static int cmd_caps(void)
{
    uint64_t caps = 0;

    if (xmod_backend_open() < 0)
        return EXIT_FAILURE;

    if (xmod_backend_query_caps(&caps) < 0) {
        xmod_backend_close();
        return EXIT_FAILURE;
    }

    printf("Xmod capabilities:\n");

    if (caps & XMOD_CAP_PROCESS_READ)
        printf("  PROCESS_READ\n");
    if (caps & XMOD_CAP_PROCESS_WRITE)
        printf("  PROCESS_WRITE\n");
    if (caps & XMOD_CAP_PROCESS_REGIONS)
        printf("  PROCESS_REGIONS\n");

    if (caps & XMOD_CAP_PHYSICAL_READ)
        printf("  PHYSICAL_READ\n");
    if (caps & XMOD_CAP_PHYSICAL_WRITE)
        printf("  PHYSICAL_WRITE\n");
    if (caps & XMOD_CAP_PHYSICAL_REGIONS)
        printf("  PHYSICAL_REGIONS\n");

    xmod_backend_close();
    return EXIT_SUCCESS;
}

enum {
    OPT_PID = 1000,
    OPT_PHYSICAL,
    OPT_ADDR,
    OPT_LEN,
    OPT_FILE,
    OPT_U8,
    OPT_U16,
    OPT_U32,
    OPT_U64,
    OPT_FLOAT,
    OPT_STRING,
    OPT_BYTES,
    OPT_YES,
};

static int cmd_rw(bool write_op, int argc, char **argv)
{
    static struct option longopts[] = {
        {"pid",      required_argument, NULL, OPT_PID},
        {"physical", no_argument,       NULL, OPT_PHYSICAL},
        {"addr",     required_argument, NULL, OPT_ADDR},
        {"len",      required_argument, NULL, OPT_LEN},
        {"file",     required_argument, NULL, OPT_FILE},
        {"u8",       required_argument, NULL, OPT_U8},
        {"u16",      required_argument, NULL, OPT_U16},
        {"u32",      required_argument, NULL, OPT_U32},
        {"u64",      required_argument, NULL, OPT_U64},
        {"float",    required_argument, NULL, OPT_FLOAT},
        {"string",   required_argument, NULL, OPT_STRING},
        {"bytes",    required_argument, NULL, OPT_BYTES},
        {"yes",      no_argument,       NULL, OPT_YES},
        {NULL,       0,                 NULL, 0},
    };

    pid_t target_pid = 0;
    bool physical = false;
    bool addr_set = false;
    uint64_t addr = 0;
    uint64_t len = 0;
    const char *outfile = NULL;
    bool assume_yes = false;

    uint8_t value_buf[MAX_VALUE];
    size_t value_len = 0;
    bool value_set = false;

    optind = 1;

    int c;
    while ((c = getopt_long(argc, argv, "", longopts, NULL)) != -1) {
        switch (c) {
        case OPT_PID:
            target_pid = (pid_t)parse_u64(optarg);
            break;

        case OPT_PHYSICAL:
            physical = true;
            break;

        case OPT_ADDR:
            addr = parse_u64(optarg);
            addr_set = true;
            break;

        case OPT_LEN:
            len = parse_u64(optarg);
            break;

        case OPT_FILE:
            outfile = optarg;
            break;

        case OPT_U8: {
            uint64_t v = parse_u64(optarg);
            if (v > UINT8_MAX) {
                fprintf(stderr, "u8 value too large\n");
                return EXIT_FAILURE;
            }
            uint8_t tmp = (uint8_t)v;
            memcpy(value_buf, &tmp, sizeof(tmp));
            value_len = sizeof(tmp);
            value_set = true;
            break;
        }

        case OPT_U16: {
            uint64_t v = parse_u64(optarg);
            if (v > UINT16_MAX) {
                fprintf(stderr, "u16 value too large\n");
                return EXIT_FAILURE;
            }
            uint16_t tmp = (uint16_t)v;
            memcpy(value_buf, &tmp, sizeof(tmp));
            value_len = sizeof(tmp);
            value_set = true;
            break;
        }

        case OPT_U32: {
            uint64_t v = parse_u64(optarg);
            if (v > UINT32_MAX) {
                fprintf(stderr, "u32 value too large\n");
                return EXIT_FAILURE;
            }
            uint32_t tmp = (uint32_t)v;
            memcpy(value_buf, &tmp, sizeof(tmp));
            value_len = sizeof(tmp);
            value_set = true;
            break;
        }

        case OPT_U64: {
            uint64_t v = parse_u64(optarg);
            uint64_t tmp = v;
            memcpy(value_buf, &tmp, sizeof(tmp));
            value_len = sizeof(tmp);
            value_set = true;
            break;
        }

        case OPT_FLOAT: {
            char *end = NULL;
            errno = 0;
            float f = strtof(optarg, &end);
            if (errno != 0 || end == NULL || *end != '\0') {
                fprintf(stderr, "Invalid float\n");
                return EXIT_FAILURE;
            }
            memcpy(value_buf, &f, sizeof(f));
            value_len = sizeof(f);
            value_set = true;
            break;
        }

        case OPT_STRING: {
            size_t slen = strlen(optarg);
            if (slen > MAX_VALUE) {
                fprintf(stderr, "string too large\n");
                return EXIT_FAILURE;
            }
            memcpy(value_buf, optarg, slen);
            value_len = slen;
            value_set = true;
            break;
        }

        case OPT_BYTES: {
            if (hex2bin(optarg, value_buf, MAX_VALUE, &value_len) < 0) {
                fprintf(stderr, "Invalid hex bytes\n");
                return EXIT_FAILURE;
            }
            value_set = true;
            break;
        }

        case OPT_YES:
            assume_yes = true;
            break;

        default:
            usage();
            return EXIT_FAILURE;
        }
    }

    if (physical && target_pid != 0) {
        fprintf(stderr, "Use either --pid or --physical, not both\n");
        return EXIT_FAILURE;
    }

    if (!physical && target_pid == 0) {
        fprintf(stderr, "Missing --pid or --physical\n");
        return EXIT_FAILURE;
    }

    if (!addr_set) {
        fprintf(stderr, "Missing --addr\n");
        return EXIT_FAILURE;
    }

    if (!write_op && len == 0) {
        fprintf(stderr, "Missing --len\n");
        return EXIT_FAILURE;
    }

    if (write_op && !value_set) {
        fprintf(stderr, "Missing value: --u8/--u16/--u32/--u64/--float/--string/--bytes\n");
        return EXIT_FAILURE;
    }

    if (!write_op && value_set) {
        fprintf(stderr, "Read mode does not take a value\n");
        return EXIT_FAILURE;
    }

    if (xmod_backend_open() < 0)
        return EXIT_FAILURE;

    uint32_t handle = 0;

    if (physical) {
        if (write_op) {
            if (!confirm("Physical memory write can corrupt the system.", assume_yes)) {
                xmod_backend_close();
                return EXIT_FAILURE;
            }

            if (xmod_backend_open_physical(false, true, &handle) < 0) {
                xmod_backend_close();
                return EXIT_FAILURE;
            }
        } else {
            if (!confirm("Physical memory read can expose sensitive system data.", assume_yes)) {
                xmod_backend_close();
                return EXIT_FAILURE;
            }

            if (xmod_backend_open_physical(true, false, &handle) < 0) {
                xmod_backend_close();
                return EXIT_FAILURE;
            }
        }
    } else {
        if (xmod_backend_open_process(target_pid, write_op, &handle) < 0) {
            xmod_backend_close();
            return EXIT_FAILURE;
        }
    }

    int ret = EXIT_SUCCESS;

    if (!write_op) {
        uint8_t *chunk = malloc(CHUNK_SIZE);
        if (!chunk) {
            perror("malloc");
            ret = EXIT_FAILURE;
            goto close_target;
        }

        FILE *fp = NULL;
        bool raw_stdout = false;

        if (outfile != NULL) {
            if (strcmp(outfile, "-") == 0) {
                fp = stdout;
                raw_stdout = true;
            } else {
                fp = fopen(outfile, "wb");
                if (!fp) {
                    perror("fopen outfile");
                    free(chunk);
                    ret = EXIT_FAILURE;
                    goto close_target;
                }
            }
        }

        uint64_t cur = addr;
        uint64_t remaining = len;

        while (remaining > 0) {
            size_t want = remaining > CHUNK_SIZE ? CHUNK_SIZE : (size_t)remaining;
            size_t done = 0;

            if (xmod_backend_read(handle, cur, chunk, want, &done) < 0) {
                ret = EXIT_FAILURE;
                break;
            }

            if (done == 0)
                break;

            if (fp != NULL) {
                if (fwrite(chunk, 1, done, fp) != done) {
                    fprintf(stderr, "Short write to output file\n");
                    ret = EXIT_FAILURE;
                    break;
                }
            } else {
                hexdump(cur, chunk, done);
            }

            cur += done;
            remaining -= done;

            if (done < want)
                break;
        }

        if (fp != NULL && fp != stdout)
            fclose(fp);

        if (fp == stdout && raw_stdout)
            fflush(stdout);

        free(chunk);
    } else {
        size_t done = 0;

        if (xmod_backend_write(handle, addr, value_buf, value_len, &done) < 0) {
            ret = EXIT_FAILURE;
            goto close_target;
        }

        if (done != value_len) {
            fprintf(stderr, "Partial write: %zu/%zu\n", done, value_len);
            ret = EXIT_FAILURE;
        } else {
            printf("Wrote %zu bytes to 0x%016llx\n",
                   done,
                   (unsigned long long)addr);
        }
    }

close_target:
    xmod_backend_close_target(handle);
    xmod_backend_close();

    return ret;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return EXIT_FAILURE;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "caps") == 0) {
        return cmd_caps();
    }

    if (strcmp(cmd, "read") == 0) {
        return cmd_rw(false, argc - 1, argv + 1);
    }

    if (strcmp(cmd, "write") == 0) {
        return cmd_rw(true, argc - 1, argv + 1);
    }

    usage();
    return EXIT_FAILURE;
}
