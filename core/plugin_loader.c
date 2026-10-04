#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <dirent.h>
#include <limits.h>

#include "plugin_loader.h"
#include "core_api.h"
#include "backend.h"

#include <xmod/plugin.h>

static const char *plugin_dir(void)
{
    const char *dir = getenv("XMOD_PLUGIN_DIR");
    return dir ? dir : "plugins";
}

static int has_suffix(const char *s, const char *suffix)
{
    size_t ls = strlen(s);
    size_t lf = strlen(suffix);

    if (ls < lf)
        return 0;

    return strcmp(s + ls - lf, suffix) == 0;
}

int plugin_list(void)
{
    DIR *d = opendir(plugin_dir());
    if (!d) {
        perror("opendir plugin dir");
        return EXIT_FAILURE;
    }

    struct dirent *e;

    while ((e = readdir(d)) != NULL) {
        if (!has_suffix(e->d_name, ".so"))
            continue;

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", plugin_dir(), e->d_name);

        void *so = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!so)
            continue;

        const xmod_plugin_info_t *(*info_fn)(void) =
            (const xmod_plugin_info_t *(*)(void))dlsym(so, "xmod_plugin_info");

        if (info_fn) {
            const xmod_plugin_info_t *info = info_fn();

            if (info && info->abi_version == XMOD_PLUGIN_ABI) {
                printf("%-12s %-8s %s\n",
                       info->name ? info->name : "?",
                       info->version ? info->version : "?",
                       info->description ? info->description : "");
            }
        }

        dlclose(so);
    }

    closedir(d);
    return EXIT_SUCCESS;
}

int plugin_run(const char *name, int argc, char **argv)
{
    char path[PATH_MAX];

    if (strchr(name, '/')) {
        snprintf(path, sizeof(path), "%s", name);
    } else {
        snprintf(path, sizeof(path), "%s/%s.so", plugin_dir(), name);
    }

    void *so = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!so) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return EXIT_FAILURE;
    }

    const xmod_plugin_info_t *(*info_fn)(void) =
        (const xmod_plugin_info_t *(*)(void))dlsym(so, "xmod_plugin_info");

    const xmod_plugin_ops_t *(*ops_fn)(void) =
        (const xmod_plugin_ops_t *(*)(void))dlsym(so, "xmod_plugin_ops");

    if (!info_fn || !ops_fn) {
        fprintf(stderr, "Plugin missing xmod_plugin_info/xmod_plugin_ops\n");
        dlclose(so);
        return EXIT_FAILURE;
    }

    const xmod_plugin_info_t *info = info_fn();
    const xmod_plugin_ops_t *ops = ops_fn();

    if (!info || info->abi_version != XMOD_PLUGIN_ABI) {
        fprintf(stderr, "Plugin ABI mismatch\n");
        dlclose(so);
        return EXIT_FAILURE;
    }

    if (!ops || !ops->run) {
        fprintf(stderr, "Plugin has no run function\n");
        dlclose(so);
        return EXIT_FAILURE;
    }

    xmod_core_t core = {0};

    if (xmod_backend_open() < 0) {
        dlclose(so);
        return EXIT_FAILURE;
    }

    int ret = ops->run(&core, &xmod_core_api, argc, argv);

    xmod_backend_close();
    dlclose(so);

    return ret == EXIT_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
