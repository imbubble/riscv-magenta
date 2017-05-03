// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/compiler.h>

#include "devhost.h"
#include <driver/driver-api.h>

// These are the API entry-points from drivers
// They must take the devhost_api_lock before calling devhost_* internals
//
// Driver code MUST NOT directly call devhost_* APIs

void driver_add(mx_driver_t* drv) {
    DM_LOCK();
    devhost_driver_add(drv);
    DM_UNLOCK();
}

void driver_remove(mx_driver_t* drv) {
    DM_LOCK();
    devhost_driver_remove(drv);
    DM_UNLOCK();
}

static void _driver_unbind(mx_driver_t* drv, mx_device_t* dev) {
    DM_LOCK();
    devhost_driver_unbind(drv, dev);
    DM_UNLOCK();
}

static mx_status_t _device_create(const char* name, void* ctx, mx_protocol_device_t* ops,
                                  mx_driver_t* drv, mx_device_t** out) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_create(name, ctx, ops, drv, out);
    DM_UNLOCK();
    return r;
}

static void _device_set_protocol(mx_device_t* dev, uint32_t proto_id, void* proto_ops) {
    DM_LOCK();
    devhost_device_set_protocol(dev, proto_id, proto_ops);
    DM_UNLOCK();
}

static mx_status_t _device_add(mx_device_t* dev, mx_device_t* parent,
                               mx_device_prop_t* props, uint32_t prop_count,
                               const char* businfo, mx_handle_t resource) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_add(dev, parent, props, prop_count, businfo, resource);
    DM_UNLOCK();
    return r;
}

static mx_status_t _device_add_instance(mx_device_t* dev, mx_device_t* parent) {
    mx_status_t r;
    DM_LOCK();
    if (dev) {
        dev->flags |= DEV_FLAG_INSTANCE | DEV_FLAG_UNBINDABLE;
    }
    r = devhost_device_add(dev, parent, NULL, 0, NULL, 0);
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

static mx_status_t _device_rebind(mx_device_t* dev) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_rebind(dev);
    DM_UNLOCK();
    return r;
}

static void _device_destroy(mx_device_t* dev) {
    DM_LOCK();
    devhost_device_destroy(dev);
    DM_UNLOCK();
}

static void _device_set_bindable(mx_device_t* dev, bool bindable) {
    DM_LOCK();
    devhost_device_set_bindable(dev, bindable);
    DM_UNLOCK();
}

mx_status_t device_bind(mx_device_t* dev, const char* drv_name) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_bind(dev, drv_name);
    DM_UNLOCK();
    return r;
}

mx_status_t device_openat(mx_device_t* dev, mx_device_t** out, const char* path, uint32_t flags) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_openat(dev, out, path, flags);
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

extern mx_handle_t root_resource_handle;

__EXPORT mx_handle_t _get_root_resource(void) {
    return root_resource_handle;
}

static mx_status_t _load_firmware(mx_driver_t* drv, const char* path, mx_handle_t* fw,
                                  size_t* size) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_load_firmware(drv, path, fw, size);
    DM_UNLOCK();
    return r;
}

driver_api_t devhost_api = {
    .driver_unbind = _driver_unbind,
    .device_create = _device_create,
    .device_set_protocol = _device_set_protocol,
    .device_add = _device_add,
    .device_add_instance = _device_add_instance,
    .device_remove = _device_remove,
    .device_rebind = _device_rebind,
    .device_destroy = _device_destroy,
    .device_set_bindable = _device_set_bindable,
    .get_root_resource = _get_root_resource,
    .load_firmware = _load_firmware,
};
