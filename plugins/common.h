#ifndef XMOD_PLUGIN_COMMON_H
#define XMOD_PLUGIN_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#define XPLUG_CHUNK (1024 * 1024)
#define XPLUG_MAX_PATTERN 4096

typedef enum xplug_type {
    XTYPE_U8,
    XTYPE_U16,
    XTYPE_U32,
    XTYPE_U64,
    XTYPE_FLOAT,
} xplug_type_t;

typedef struct xplug_pattern {
    uint8_t bytes[XPLUG_MAX_PATTERN];
    uint8_t mask[XPLUG_MAX_PATTERN];
    size_t len;
} xplug_pattern_t;

static inline int xplug_hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static inline uint64_t xplug_parse_u64(const char *s)
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

static inline int xplug_parse_type(const char *s, xplug_type_t *out)
{
    if (!strcmp(s, "u8") || !strcmp(s, "uint8") || !strcmp(s, "byte")) {
        *out = XTYPE_U8;
        return 0;
    }

    if (!strcmp(s, "u16") || !strcmp(s, "uint16") || !strcmp(s, "short")) {
        *out = XTYPE_U16;
        return 0;
    }

    if (!strcmp(s, "u32") || !strcmp(s, "uint32") || !strcmp(s, "int")) {
        *out = XTYPE_U32;
        return 0;
    }

    if (!strcmp(s, "u64") || !strcmp(s, "uint64") || !strcmp(s, "long")) {
        *out = XTYPE_U64;
        return 0;
    }

    if (!strcmp(s, "float") || !strcmp(s, "f32")) {
        *out = XTYPE_FLOAT;
        return 0;
    }

    return -1;
}

static inline size_t xplug_type_size(xplug_type_t t)
{
    switch (t) {
    case XTYPE_U8:
        return 1;
    case XTYPE_U16:
        return 2;
    case XTYPE_U32:
    case XTYPE_FLOAT:
        return 4;
    case XTYPE_U64:
        return 8;
    default:
        return 0;
    }
}

static inline uint64_t xplug_raw_from_bytes(const uint8_t *p, xplug_type_t t)
{
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    float f;

    switch (t) {
    case XTYPE_U8:
        memcpy(&u8, p, sizeof(u8));
        return u8;

    case XTYPE_U16:
        memcpy(&u16, p, sizeof(u16));
        return u16;

    case XTYPE_U32:
        memcpy(&u32, p, sizeof(u32));
        return u32;

    case XTYPE_U64:
        memcpy(&u64, p, sizeof(u64));
        return u64;

    case XTYPE_FLOAT:
        memcpy(&f, p, sizeof(f));
        memcpy(&u32, &f, sizeof(u32));
        return u32;

    default:
        return 0;
    }
}

static inline int xplug_parse_value(const char *s, xplug_type_t t, uint64_t *out)
{
    if (t == XTYPE_FLOAT) {
        char *end = NULL;
        errno = 0;

        float f = strtof(s, &end);
        if (errno != 0 || end == NULL || *end != '\0')
            return -1;

        uint32_t u32;
        memcpy(&u32, &f, sizeof(u32));
        *out = u32;
        return 0;
    }

    uint64_t v = xplug_parse_u64(s);

    switch (t) {
    case XTYPE_U8:
        if (v > UINT8_MAX)
            return -1;
        break;
    case XTYPE_U16:
        if (v > UINT16_MAX)
            return -1;
        break;
    case XTYPE_U32:
        if (v > UINT32_MAX)
            return -1;
        break;
    default:
        break;
    }

    *out = v;
    return 0;
}

static inline int xplug_parse_pattern(const char *input, xplug_pattern_t *pat)
{
    char *s = strdup(input);
    if (!s)
        return -1;

    memset(pat, 0, sizeof(*pat));

    char *save = NULL;

    for (char *tok = strtok_r(s, " ,", &save);
         tok != NULL;
         tok = strtok_r(NULL, " ,", &save)) {

        if (pat->len >= XPLUG_MAX_PATTERN) {
            free(s);
            return -1;
        }

        if (tok[0] == '?' || tok[1] == '?') {
            pat->bytes[pat->len] = 0;
            pat->mask[pat->len] = 0;
        } else {
            int hi = xplug_hexval(tok[0]);
            int lo = xplug_hexval(tok[1]);

            if (hi < 0 || lo < 0 || tok[2] != '\0') {
                free(s);
                return -1;
            }

            pat->bytes[pat->len] = (uint8_t)((hi << 4) | lo);
            pat->mask[pat->len] = 1;
        }

        pat->len++;
    }

    free(s);

    return pat->len > 0 ? 0 : -1;
}

static inline int xplug_pattern_match(const uint8_t *buf,
                                      const xplug_pattern_t *pat)
{
    for (size_t i = 0; i < pat->len; i++) {
        if (pat->mask[i] && buf[i] != pat->bytes[i])
            return 0;
    }

    return 1;
}

#endif
