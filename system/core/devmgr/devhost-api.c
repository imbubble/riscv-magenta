// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/compiler.h>

#include <ddk/device.h>
#include "devhost.h"
#include <driver/driver-api.h>

// These are the API entry-points from drivers
// They must take the devhost_api_lock before calling devhost_* internals
//
// Driver code MUST NOT directly call devhost_* APIs


// LibDriver Device Interface

static mx_status_t _device_add(mx_driver_t* drv, mx_device_t* parent,
                               device_add_args_t* args, mx_device_t** out) {
    mx_status_t r;
    mx_device_t* dev = NULL;

    if (!parent) {
        return MX_ERR_INVALID_ARGS;
    }
    if (!args || args->version != DEVICE_ADD_ARGS_VERSION) {
        return MX_ERR_INVALID_ARGS;
    }
    if (!args->ops || args->ops->version != DEVICE_OPS_VERSION) {
        return MX_ERR_INVALID_ARGS;
    }
    if (args->flags & ~(DEVICE_ADD_NON_BINDABLE | DEVICE_ADD_INSTANCE | DEVICE_ADD_BUSDEV)) {
        return MX_ERR_INVALID_ARGS;
    }
    if ((args->flags & DEVICE_ADD_INSTANCE) && (args->flags & DEVICE_ADD_BUSDEV)) {
        return MX_ERR_INVALID_ARGS;
    }

    DM_LOCK();
    r = devhost_device_create(drv, parent, args->name, args->ctx, args->ops, &dev);
    if (r != MX_OK) {
        DM_UNLOCK();
        return r;
    }
    if (args->proto_id) {
        dev->protocol_id = args->proto_id;
        dev->protocol_ops = args->proto_ops;
    }
    if (args->flags & DEVICE_ADD_NON_BINDABLE) {
        dev->flags |= DEV_FLAG_UNBINDABLE;
    }

    if (args->flags & DEVICE_ADD_BUSDEV) {
        r = devhost_device_add(dev, parent, args->props, args->prop_count, args->busdev_args,
                               args->rsrc);
    } else if (args->flags & DEVICE_ADD_INSTANCE) {
        dev->flags |= DEV_FLAG_INSTANCE | DEV_FLAG_UNBINDABLE;
        r = devhost_device_add(dev, parent, NULL, 0, NULL, MX_HANDLE_INVALID);
    } else {
        r = devhost_device_add(dev, parent, args->props, args->prop_count, NULL, MX_HANDLE_INVALID);
    }
    if (r == MX_OK) {
        *out = dev;
    } else {
        devhost_device_destroy(dev);
    }

    DM_UNLOCK();
    return r;
}

static mx_status_t _device_remove(mx_device_t* dev) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_remove(dev);
    DM_UNLOCK();
    return r;
}

static void _device_unbind(mx_device_t* dev) {
    DM_LOCK();
    devhost_device_unbind(dev);
    DM_UNLOCK();
}

static mx_status_t _device_rebind(mx_device_t* dev) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_rebind(dev);
    DM_UNLOCK();
    return r;
}


const char* _device_get_name(mx_device_t* dev) {
    return dev->name;
}

mx_device_t* _device_get_parent(mx_device_t* dev) {
    return dev->parent;
}

mx_status_t _device_get_protocol(mx_device_t* dev, uint32_t proto_id,
                                 void** protocol) {
    if (dev->ops->get_protocol) {
        return dev->ops->get_protocol(dev->ctx, proto_id, protocol);
    }
    if (proto_id == MX_PROTOCOL_DEVICE) {
        *protocol = dev->ops;
        return NO_ERROR;
    }
    if ((proto_id == dev->protocol_id) && (dev->protocol_ops != NULL)) {
        *protocol = dev->protocol_ops;
        return NO_ERROR;
    }
    return ERR_NOT_SUPPORTED;
}

mx_handle_t _device_get_resource(mx_device_t* dev) {
    mx_handle_t h;
    if (mx_handle_duplicate(dev->resource, MX_RIGHT_SAME_RIGHTS, &h) < 0) {
        return MX_HANDLE_INVALID;
    } else {
        return h;
    }
}

void _device_state_clr_set(mx_device_t* dev, mx_signals_t clearflag, mx_signals_t setflag) {
    mx_object_signal(dev->event, clearflag, setflag);
}


mx_off_t _device_op_get_size(mx_device_t* dev) {
    return dev->ops->get_size(dev->ctx);
}

mx_status_t _device_op_read(mx_device_t* dev, void* buf, size_t count,
                            mx_off_t off, size_t* actual) {
    return dev->ops->read(dev->ctx, buf, count, off, actual);
}

mx_status_t _device_op_write(mx_device_t* dev, const void* buf, size_t count,
                             mx_off_t off, size_t* actual) {
    return dev->ops->write(dev->ctx, buf, count, off, actual);
}

mx_status_t _device_op_ioctl(mx_device_t* dev, uint32_t op,
                             const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {
    return dev->ops->ioctl(dev->ctx, op, in_buf, in_len, out_buf, out_len, out_actual);
}

mx_status_t _device_op_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    if (dev->ops->iotxn_queue != NULL) {
        dev->ops->iotxn_queue(dev->ctx, txn);
        return NO_ERROR;
    } else {
        return ERR_NOT_SUPPORTED;
    }
}


// LibDriver Misc Interfaces

extern mx_handle_t root_resource_handle;

__EXPORT mx_handle_t _get_root_resource(void) {
    return root_resource_handle;
}

static mx_status_t _load_firmware(mx_device_t* dev, const char* path, mx_handle_t* fw,
                                  size_t* size) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_load_firmware(dev, path, fw, size);
    DM_UNLOCK();
    return r;
}


// Interface Used by DevHost RPC Layer

mx_status_t device_bind(mx_device_t* dev, const char* drv_libname) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_bind(dev, drv_libname);
    DM_UNLOCK();
    return r;
}

mx_status_t device_open_at(mx_device_t* dev, mx_device_t** out, const char* path, uint32_t flags) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_open_at(dev, out, path, flags);
    DM_UNLOCK();
    return r;
}

mx_status_t device_close(mx_device_t* dev, uint32_t flags) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_close(dev, flags);
    DM_UNLOCK();
    return r;
}


driver_api_t devhost_api = {
    .add = _device_add,
    .remove = _device_remove,
    .unbind = _device_unbind,
    .rebind = _device_rebind,
    .get_name = _device_get_name,
    .get_parent = _device_get_parent,
    .get_protocol = _device_get_protocol,
    .get_resource = _device_get_resource,
    .state_clr_set = _device_state_clr_set,
    .op_get_size = _device_op_get_size,
    .op_read = _device_op_read,
    .op_write = _device_op_write,
    .op_ioctl = _device_op_ioctl,
    .op_iotxn_queue = _device_op_iotxn_queue,
    .get_root_resource = _get_root_resource,
    .load_firmware = _load_firmware,
};
