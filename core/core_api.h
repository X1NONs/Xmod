#ifndef XMOD_CORE_API_H
#define XMOD_CORE_API_H

#include <xmod/plugin.h>

struct xmod_core {
    int backend_open;
};

extern const xmod_core_api_t xmod_core_api;

#endif
