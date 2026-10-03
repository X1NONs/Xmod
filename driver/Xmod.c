// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/kref.h>
#include <linux/rcupdate.h>
#include <linux/capability.h>

#include "../include/xmod/uapi.h"

#define XMOD_MAX_RW (16 * 1024 * 1024)

struct xmod_handle {
    struct kref ref;
    enum xmod_target_type type;
    u64 caps;

    struct task_struct *task;
};

static DEFINE_IDR(xmod_idr);
static DEFINE_MUTEX(xmod_idr_lock);

static void xmod_handle_release(struct kref *ref)
{
    struct xmod_handle *h = container_of(ref, struct xmod_handle, ref);

    if (h->task)
        put_task_struct(h->task);

    kfree(h);
}

static void xmod_handle_put(struct xmod_handle *h)
{
    if (h)
        kref_put(&h->ref, xmod_handle_release);
}

static struct xmod_handle *xmod_handle_get(u32 id)
{
    struct xmod_handle *h = NULL;

    mutex_lock(&xmod_idr_lock);
    h = idr_find(&xmod_idr, id);
    if (h && !kref_get_unless_zero(&h->ref))
        h = NULL;
    mutex_unlock(&xmod_idr_lock);

    return h;
}

static int xmod_handle_add(struct xmod_handle *h, u32 *out_id)
{
    int id;

    mutex_lock(&xmod_idr_lock);
    id = idr_alloc(&xmod_idr, h, 1, 0, GFP_KERNEL);
    mutex_unlock(&xmod_idr_lock);

    if (id < 0)
        return id;

    *out_id = (u32)id;
    return 0;
}

static void xmod_handle_remove(u32 id)
{
    struct xmod_handle *h;

    mutex_lock(&xmod_idr_lock);
    h = idr_remove(&xmod_idr, id);
    mutex_unlock(&xmod_idr_lock);

    if (h)
        xmod_handle_put(h);
}

static int xmod_handshake(struct xmod_handshake __user *arg)
{
    struct xmod_handshake h;

    if (copy_from_user(&h, arg, sizeof(h)))
        return -EFAULT;

    if (h.version != XMOD_API_VERSION)
        return -EINVAL;

    return 0;
}

static int xmod_query_caps(struct xmod_query_caps __user *arg)
{
    struct xmod_query_caps caps = {
        .version = XMOD_API_VERSION,
        .flags = 0,
        .global_caps = 0,
    };

    caps.global_caps |= XMOD_CAP_PROCESS_READ;
    caps.global_caps |= XMOD_CAP_PROCESS_WRITE;
    caps.global_caps |= XMOD_CAP_PROCESS_REGIONS;

    caps.global_caps |= XMOD_CAP_PHYSICAL_READ;
    caps.global_caps |= XMOD_CAP_PHYSICAL_WRITE;
    caps.global_caps |= XMOD_CAP_PHYSICAL_REGIONS;

    if (copy_to_user(arg, &caps, sizeof(caps)))
        return -EFAULT;

    return 0;
}

static int xmod_process_open(struct xmod_open_target __user *arg,
                             struct xmod_open_target *req)
{
    struct task_struct *task;
    struct xmod_handle *h;
    u32 handle_id = 0;
    int ret;

    if (req->id == 0)
        return -EINVAL;

    if (!capable(CAP_SYS_PTRACE))
        return -EPERM;

    rcu_read_lock();
    task = pid_task(find_vpid(req->id), PIDTYPE_PID);
    if (task)
        get_task_struct(task);
    rcu_read_unlock();

    if (!task)
        return -ESRCH;

    h = kzalloc(sizeof(*h), GFP_KERNEL);
    if (!h) {
        put_task_struct(task);
        return -ENOMEM;
    }

    kref_init(&h->ref);
    h->type = XMOD_TARGET_PROCESS;
    h->task = task;

    h->caps = XMOD_CAP_PROCESS_READ | XMOD_CAP_PROCESS_REGIONS;

    if (req->flags & XMOD_OPEN_FLAG_WRITE)
        h->caps |= XMOD_CAP_PROCESS_WRITE;

    ret = xmod_handle_add(h, &handle_id);
    if (ret) {
        put_task_struct(task);
        kfree(h);
        return ret;
    }

    req->out_handle = handle_id;

    if (copy_to_user(arg, req, sizeof(*req))) {
        xmod_handle_remove(handle_id);
        return -EFAULT;
    }

    return 0;
}

static int xmod_physical_open(struct xmod_open_target __user *arg,
                              struct xmod_open_target *req)
{
    struct xmod_handle *h;
    u32 handle_id = 0;
    int ret;

    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

    if (!(req->flags & (XMOD_OPEN_FLAG_READ | XMOD_OPEN_FLAG_WRITE)))
        return -EINVAL;

    h = kzalloc(sizeof(*h), GFP_KERNEL);
    if (!h)
        return -ENOMEM;

    kref_init(&h->ref);
    h->type = XMOD_TARGET_PHYSICAL;

    if (req->flags & XMOD_OPEN_FLAG_READ)
        h->caps |= XMOD_CAP_PHYSICAL_READ | XMOD_CAP_PHYSICAL_REGIONS;

    if (req->flags & XMOD_OPEN_FLAG_WRITE)
        h->caps |= XMOD_CAP_PHYSICAL_WRITE;

    ret = xmod_handle_add(h, &handle_id);
    if (ret) {
        kfree(h);
        return ret;
    }

    req->out_handle = handle_id;

    if (copy_to_user(arg, req, sizeof(*req))) {
        xmod_handle_remove(handle_id);
        return -EFAULT;
    }

    return 0;
}

static int xmod_open_target(struct xmod_open_target __user *arg)
{
    struct xmod_open_target req;

    if (copy_from_user(&req, arg, sizeof(req)))
        return -EFAULT;

    if (req.version != XMOD_API_VERSION)
        return -EINVAL;

    switch (req.type) {
    case XMOD_TARGET_PROCESS:
        return xmod_process_open(arg, &req);
    case XMOD_TARGET_PHYSICAL:
        return xmod_physical_open(arg, &req);
    default:
        return -EINVAL;
    }
}

static int xmod_process_rw(struct xmod_handle *h, bool write,
                           struct xmod_rw_req *req)
{
    void *buf;
    int ret;
    size_t len = (size_t)req->length;

    if (h->type != XMOD_TARGET_PROCESS)
        return -EINVAL;

    if (write && !(h->caps & XMOD_CAP_PROCESS_WRITE))
        return -EPERM;

    if (!write && !(h->caps & XMOD_CAP_PROCESS_READ))
        return -EPERM;

    if (len == 0 || len > XMOD_MAX_RW)
        return -EINVAL;

    if (req->remote_addr > (u64)(unsigned long)-1)
        return -ERANGE;

    buf = kvmalloc(len, GFP_KERNEL);
    if (!buf)
        return -ENOMEM;

    if (write) {
        if (copy_from_user(buf, (void __user *)req->user_ptr, len)) {
            kvfree(buf);
            return -EFAULT;
        }
    }

    ret = access_process_vm(h->task,
                            (unsigned long)req->remote_addr,
                            buf,
                            (int)len,
                            write ? FOLL_WRITE : 0);

    if (ret < 0) {
        kvfree(buf);
        return ret;
    }

    if (!write && ret > 0) {
        if (copy_to_user((void __user *)req->user_ptr, buf, ret)) {
            kvfree(buf);
            return -EFAULT;
        }
    }

    req->bytes_done = (u64)ret;
    kvfree(buf);

    return 0;
}

static int xmod_physical_rw(struct xmod_handle *h, bool write,
                            struct xmod_rw_req *req)
{
    void *buf = NULL;
    void *kaddr = NULL;
    bool is_io = false;
    size_t len = (size_t)req->length;
    int ret = 0;

    if (h->type != XMOD_TARGET_PHYSICAL)
        return -EINVAL;

    if (write && !(h->caps & XMOD_CAP_PHYSICAL_WRITE))
        return -EPERM;

    if (!write && !(h->caps & XMOD_CAP_PHYSICAL_READ))
        return -EPERM;

    if (len == 0 || len > XMOD_MAX_RW)
        return -EINVAL;

    if (req->remote_addr + req->length < req->remote_addr)
        return -EINVAL;

    buf = kvmalloc(len, GFP_KERNEL);
    if (!buf)
        return -ENOMEM;

    if (write) {
        if (copy_from_user(buf, (void __user *)req->user_ptr, len)) {
            ret = -EFAULT;
            goto out_free;
        }
    }

    kaddr = memremap((phys_addr_t)req->remote_addr, len, MEMREMAP_WB);
    if (!kaddr) {
        kaddr = ioremap((phys_addr_t)req->remote_addr, len);
        if (!kaddr) {
            ret = -EIO;
            goto out_free;
        }
        is_io = true;
    }

    if (write) {
        if (is_io)
            memcpy_toio(kaddr, buf, len);
        else
            memcpy(kaddr, buf, len);

        req->bytes_done = req->length;
    } else {
        if (is_io)
            memcpy_fromio(buf, kaddr, len);
        else
            memcpy(buf, kaddr, len);

        if (copy_to_user((void __user *)req->user_ptr, buf, len))
            ret = -EFAULT;
        else
            req->bytes_done = req->length;
    }

    if (is_io)
        iounmap(kaddr);
    else
        memunmap(kaddr);

out_free:
    kvfree(buf);
    return ret;
}

static int xmod_rw(bool write, struct xmod_rw_req __user *arg)
{
    struct xmod_rw_req req;
    struct xmod_handle *h;
    int ret;

    if (copy_from_user(&req, arg, sizeof(req)))
        return -EFAULT;

    if (req.version != XMOD_API_VERSION)
        return -EINVAL;

    h = xmod_handle_get(req.handle);
    if (!h)
        return -EINVAL;

    switch (h->type) {
    case XMOD_TARGET_PROCESS:
        ret = xmod_process_rw(h, write, &req);
        break;
    case XMOD_TARGET_PHYSICAL:
        ret = xmod_physical_rw(h, write, &req);
        break;
    default:
        ret = -EINVAL;
        break;
    }

    if (ret == 0) {
        if (copy_to_user(arg, &req, sizeof(req)))
            ret = -EFAULT;
    }

    xmod_handle_put(h);
    return ret;
}

static long xmod_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
    case XMOD_CMD_HANDSHAKE:
        return xmod_handshake((struct xmod_handshake __user *)arg);

    case XMOD_CMD_QUERY_CAPS:
        return xmod_query_caps((struct xmod_query_caps __user *)arg);

    case XMOD_CMD_OPEN_TARGET:
        return xmod_open_target((struct xmod_open_target __user *)arg);

    case XMOD_CMD_CLOSE_TARGET: {
        u32 handle;

        if (copy_from_user(&handle, (void __user *)arg, sizeof(handle)))
            return -EFAULT;

        xmod_handle_remove(handle);
        return 0;
    }

    case XMOD_CMD_READ:
        return xmod_rw(false, (struct xmod_rw_req __user *)arg);

    case XMOD_CMD_WRITE:
        return xmod_rw(true, (struct xmod_rw_req __user *)arg);

    default:
        return -ENOTTY;
    }
}

static const struct file_operations xmod_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = xmod_ioctl,
    .compat_ioctl   = xmod_ioctl,
};

static struct miscdevice xmod_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "xmod",
    .fops  = &xmod_fops,
    .mode  = 0600,
};

static int xmod_idr_destroy_cb(int id, void *p, void *data)
{
    struct xmod_handle *h = p;

    (void)id;
    (void)data;

    xmod_handle_put(h);
    return 0;
}

static int __init xmod_init(void)
{
    int ret;

    ret = misc_register(&xmod_misc);
    if (ret)
        return ret;

    pr_info("xmod: loaded\n");
    return 0;
}

static void __exit xmod_exit(void)
{
    mutex_lock(&xmod_idr_lock);
    idr_for_each(&xmod_idr, xmod_idr_destroy_cb, NULL);
    idr_destroy(&xmod_idr);
    mutex_unlock(&xmod_idr_lock);

    misc_deregister(&xmod_misc);
    pr_info("xmod: unloaded\n");
}

module_init(xmod_init);
module_exit(xmod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("arya");
MODULE_DESCRIPTION("Xmod memory access backend");
MODULE_VERSION("0.1.0");
