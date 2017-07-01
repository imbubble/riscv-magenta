// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <lib/ktrace.h>

#include <magenta/handle_owner.h>
#include <magenta/magenta.h>
#include <magenta/port_dispatcher.h>
#include <magenta/process_dispatcher.h>
#include <magenta/syscalls/policy.h>
#include <magenta/user_copy.h>

#include <mxalloc/new.h>
#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

mx_status_t sys_port_create(uint32_t options, user_ptr<mx_handle_t> _out) {
    LTRACEF("options %u\n", options);

    // Currently, the only allowed option is to switch on PortsV2.
    if (options != 0u)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    mx_status_t res = up->QueryPolicy(MX_POL_NEW_PORT);
    if (res < 0)
        return res;

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;

    mx_status_t result = PortDispatcher::Create(options, &dispatcher, &rights);

    if (result != MX_OK)
        return result;

    uint32_t koid = (uint32_t)dispatcher->get_koid();

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return MX_ERR_NO_MEMORY;

    mx_handle_t hv = up->MapHandleToValue(handle);

    if (_out.copy_to_user(hv) != MX_OK)
        return MX_ERR_INVALID_ARGS;
    up->AddHandle(mxtl::move(handle));

    ktrace(TAG_PORT_CREATE, koid, 0, 0, 0);
    return MX_OK;
}

mx_status_t sys_port_queue(mx_handle_t handle, user_ptr<const void> _packet, size_t size) {
    LTRACEF("handle %d\n", handle);

    if (size != 0u)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcher> port;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &port);
    if (status != MX_OK)
        return status;

    mx_port_packet_t packet;
    if (_packet.copy_array_from_user(&packet, sizeof(packet)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    return port->QueueUser(packet);
}

mx_status_t sys_port_wait(mx_handle_t handle, mx_time_t deadline,
                          user_ptr<void> _packet, size_t size) {
    LTRACEF("handle %d\n", handle);

    if (size != 0u)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcher> port;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_READ, &port);
    if (status != MX_OK)
        return status;

    ktrace(TAG_PORT_WAIT, (uint32_t)port->get_koid(), 0, 0, 0);

    mx_port_packet_t pp;
    mx_status_t st = port->DeQueue(deadline, &pp);

    ktrace(TAG_PORT_WAIT_DONE, (uint32_t)port->get_koid(), st, 0, 0);

    if (st != MX_OK)
        return st;

    // remove internal flag bits
    pp.type &= PKT_FLAG_MASK;

    if (_packet.copy_array_to_user(&pp, sizeof(pp)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    return MX_OK;
}

mx_status_t sys_port_cancel(mx_handle_t handle, mx_handle_t source, uint64_t key) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcher> port;
    mx_status_t status = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &port);
    if (status != MX_OK)
        return status;

    {
        AutoLock lock(up->handle_table_lock());
        Handle* watched = up->GetHandleLocked(source);
        if (!watched)
            return MX_ERR_BAD_HANDLE;
        if (!magenta_rights_check(watched, MX_RIGHT_READ))
            return MX_ERR_ACCESS_DENIED;

        auto state_tracker = watched->dispatcher()->get_state_tracker();
        if (!state_tracker)
            return MX_ERR_NOT_SUPPORTED;

        bool had_observer = state_tracker->CancelByKey(watched, port.get(), key);
        bool packet_removed = port->CancelQueued(watched, key);
        return (had_observer || packet_removed) ? MX_OK : MX_ERR_NOT_FOUND;
    }
}
