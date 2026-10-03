#ifndef _UAPI_XMOD_H
#define _UAPI_XMOD_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define XMOD_MAGIC 'x'
#define XMOD_API_VERSION 1

enum xmod_target_type {
    XMOD_TARGET_PROCESS  = 1,
    XMOD_TARGET_PHYSICAL = 2,
};

#define XMOD_CAP_PROCESS_READ       (1ULL << 0)
#define XMOD_CAP_PROCESS_WRITE      (1ULL << 1)
#define XMOD_CAP_PROCESS_REGIONS    (1ULL << 2)

#define XMOD_CAP_PHYSICAL_READ      (1ULL << 8)
#define XMOD_CAP_PHYSICAL_WRITE     (1ULL << 9)
#define XMOD_CAP_PHYSICAL_REGIONS   (1ULL << 10)

#define XMOD_OPEN_FLAG_READ         (1U << 0)
#define XMOD_OPEN_FLAG_WRITE        (1U << 1)

struct xmod_handshake {
    __u32 version;
    __u32 flags;
    __u64 reserved[4];
};

struct xmod_query_caps {
    __u32 version;
    __u32 flags;
    __u64 global_caps;
    __u64 reserved[4];
};

struct xmod_open_target {
    __u32 version;
    __u32 type;
    __u32 id;
    __u32 flags;
    __u64 requested_caps;
    __u32 out_handle;
    __u32 reserved;
    __u64 reserved2[4];
};

struct xmod_rw_req {
    __u32 version;
    __u32 handle;
    __u32 flags;
    __u32 reserved;
    __u64 remote_addr;
    __u64 user_ptr;
    __u64 length;
    __u64 bytes_done;
    __u64 reserved2[4];
};

#define XMOD_CMD_HANDSHAKE        _IOWR(XMOD_MAGIC, 0x01, struct xmod_handshake)
#define XMOD_CMD_QUERY_CAPS       _IOWR(XMOD_MAGIC, 0x02, struct xmod_query_caps)
#define XMOD_CMD_OPEN_TARGET      _IOWR(XMOD_MAGIC, 0x03, struct xmod_open_target)
#define XMOD_CMD_CLOSE_TARGET     _IOW(XMOD_MAGIC, 0x04, __u32)

#define XMOD_CMD_READ             _IOWR(XMOD_MAGIC, 0x10, struct xmod_rw_req)
#define XMOD_CMD_WRITE            _IOWR(XMOD_MAGIC, 0x11, struct xmod_rw_req)

#endif /* _UAPI_XMOD_H */
