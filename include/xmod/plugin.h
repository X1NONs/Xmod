#ifndef XMOD_PLUGIN_H
#define XMOD_PLUGIN_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

#define XMOD_PLUGIN_ABI 1

typedef struct xmod_core xmod_core_t;

typedef int (*xmod_region_cb)(uint64_t start,
                              uint64_t end,
                              const char perms[5],
                              void *ctx);

typedef struct xmod_core_api {
    int (*open_process)(xmod_core_t *core,
                        pid_t pid,
                        bool write,
                        uint32_t *handle);

    int (*open_physical)(xmod_core_t *core,
                         bool read,
                         bool write,
                         uint32_t *handle);

    int (*read)(xmod_core_t *core,
                uint32_t handle,
                uint64_t addr,
                void *buf,
                size_t len,
                size_t *done);

    int (*write)(xmod_core_t *core,
                 uint32_t handle,
                 uint64_t addr,
                 const void *buf,
                 size_t len,
                 size_t *done);

    int (*close_target)(xmod_core_t *core,
                        uint32_t handle);

    int (*for_each_region)(xmod_core_t *core,
                           pid_t pid,
                           xmod_region_cb cb,
                           void *ctx);

    bool (*confirm)(xmod_core_t *core,
                    const char *message,
                    bool assume_yes);

    void (*log)(xmod_core_t *core,
                const char *fmt,
                ...);
} xmod_core_api_t;

typedef struct xmod_plugin_info {
    uint32_t abi_version;
    const char *name;
    const char *version;
    const char *description;
} xmod_plugin_info_t;

typedef struct xmod_plugin_ops {
    int (*run)(xmod_core_t *core,
               const xmod_core_api_t *api,
               int argc,
               char **argv);
} xmod_plugin_ops_t;

#endif /* XMOD_PLUGIN_H */
