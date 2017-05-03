// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/socket_dispatcher.h>

#include <string.h>

#include <assert.h>
#include <err.h>
#include <new.h>
#include <trace.h>
#include <pow2.h>

#include <lib/user_copy/user_ptr.h>

#include <kernel/auto_lock.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>
#include <kernel/vm/vm_object_paged.h>

#include <magenta/handle.h>
#include <magenta/port_client.h>

#define LOCAL_TRACE 0

constexpr mx_rights_t kDefaultSocketRights =
    MX_RIGHT_TRANSFER | MX_RIGHT_DUPLICATE | MX_RIGHT_READ | MX_RIGHT_WRITE;

constexpr size_t kDeFaultSocketBufferSize = 256 * 1024u;

constexpr mx_signals_t kValidSignalMask =
    MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED | MX_USER_SIGNAL_ALL;

namespace {
// Cribbed from pow2.h, we need overloading to correctly deal with 32 and 64 bits.
template <typename T> T vmodpow2(T val, uint modp2) { return val & ((1U << modp2) - 1); }
}

#define INC_POINTER(len_pow2, ptr, inc) vmodpow2(((ptr) + (inc)), len_pow2)

SocketDispatcher::CBuf::~CBuf() {
    if (mapping_) {
        mapping_->Destroy();
    }
}

bool SocketDispatcher::CBuf::Init(uint32_t len) {
    vmo_ = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, len);
    if (!vmo_)
        return false;

    const uint arch_mmu_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
    auto st = VmAspace::kernel_aspace()->RootVmar()->CreateVmMapping(
            0 /* ignored */, len, 0 /* align pow2 */, 0 /* vmar flags */,
            vmo_, 0, arch_mmu_flags, "socket", &mapping_);

    if (st < 0)
        return false;

    DEBUG_ASSERT(mapping_);
    len_pow2_ = log2_uint_floor(len);
    return true;
}

size_t SocketDispatcher::CBuf::free() const {
    uint consumed = modpow2((uint)(head_ - tail_), len_pow2_);
    return valpow2(len_pow2_) - consumed - 1;
}

bool SocketDispatcher::CBuf::empty() const {
    return tail_ == head_;
}

size_t SocketDispatcher::CBuf::Write(const void* src, size_t len, bool from_user) {

    size_t write_len;
    size_t pos = 0;

    while (pos < len && (free() > 0)) {
        if (head_ >= tail_) {
            if (tail_ == 0) {
                // Special case - if tail is at position 0, we can't write all
                // the way to the end of the buffer. Otherwise, head ends up at
                // 0, head == tail, and buffer is considered "empty" again.
                write_len = MIN(valpow2(len_pow2_) - head_ - 1, len - pos);
            } else {
                // Write to the end of the buffer.
                write_len = MIN(valpow2(len_pow2_) - head_, len - pos);
            }
        } else {
            // Write from head to tail-1.
            write_len = MIN(tail_ - head_ - 1, len - pos);
        }

        // if it's full, abort and return how much we've written
        if (write_len == 0) {
            break;
        }

        const char *ptr = (const char*)src;
        ptr += pos;
        if (from_user) {
            // TODO: find a safer way to do this
            user_ptr<const void> uptr(ptr);
            vmo_->WriteUser(uptr, head_, write_len, nullptr);
        } else {
            memcpy(reinterpret_cast<void*>(mapping_->base() + head_), ptr, write_len);
        }

        head_ = INC_POINTER(len_pow2_, head_, write_len);
        pos += write_len;
    }
    return pos;
}

size_t SocketDispatcher::CBuf::Read(void* dest, size_t len, bool from_user) {
    size_t ret = 0;

    if (tail_ != head_) {
        size_t pos = 0;
        // loop until we've read everything we need
        // at most this will make two passes to deal with wraparound
        while (pos < len && tail_ != head_) {
            size_t read_len;
            if (head_ > tail_) {
                // simple case where there is no wraparound
                read_len = MIN(head_ - tail_, len - pos);
            } else {
                // read to the end of buffer in this pass
                read_len = MIN(valpow2(len_pow2_) - tail_, len - pos);
            }

            char *ptr = (char*)dest;
            ptr += pos;
            if (from_user) {
                // TODO: find a safer way to do this
                user_ptr<void> uptr(ptr);
                vmo_->ReadUser(uptr, tail_, read_len, nullptr);
            } else {
                memcpy(ptr, reinterpret_cast<void*>(mapping_->base() + tail_), read_len);
            }

            tail_ = INC_POINTER(len_pow2_, tail_, read_len);
            pos += read_len;
        }
        ret = pos;
    }
    return ret;
}

size_t SocketDispatcher::CBuf::CouldRead() const {
    return modpow2((uint)(head_ - tail_), len_pow2_);
}

// static
status_t SocketDispatcher::Create(uint32_t flags,
                                  mxtl::RefPtr<Dispatcher>* dispatcher0,
                                  mxtl::RefPtr<Dispatcher>* dispatcher1,
                                  mx_rights_t* rights) {
    LTRACE_ENTRY;

    AllocChecker ac;
    auto socket0 = mxtl::AdoptRef(new (&ac) SocketDispatcher(flags));
    if (!ac.check())
        return ERR_NO_MEMORY;

    auto socket1 = mxtl::AdoptRef(new (&ac) SocketDispatcher(flags));
    if (!ac.check())
        return ERR_NO_MEMORY;

    mx_status_t status;
    if ((status = socket0->Init(socket1)) != NO_ERROR)
        return status;
    if ((status = socket1->Init(socket0)) != NO_ERROR)
        return status;

    *rights = kDefaultSocketRights;
    *dispatcher0 = mxtl::RefPtr<Dispatcher>(socket0.get());
    *dispatcher1 = mxtl::RefPtr<Dispatcher>(socket1.get());
    return NO_ERROR;
}

SocketDispatcher::SocketDispatcher(uint32_t /*flags*/)
    : peer_koid_(0u),
      state_tracker_(MX_SOCKET_WRITABLE),
      half_closed_{false, false} {
}

SocketDispatcher::~SocketDispatcher() {
}

// This is called before either SocketDispatcher is accessible from threads other than the one
// initializing the socket, so it does not need locking.
mx_status_t SocketDispatcher::Init(mxtl::RefPtr<SocketDispatcher> other) TA_NO_THREAD_SAFETY_ANALYSIS {
    other_ = mxtl::move(other);
    peer_koid_ = other_->get_koid();
    return cbuf_.Init(kDeFaultSocketBufferSize) ? NO_ERROR : ERR_NO_MEMORY;
}

void SocketDispatcher::on_zero_handles() {
    canary_.Assert();

    mxtl::RefPtr<SocketDispatcher> socket;
    {
        AutoLock lock(&lock_);
        socket = mxtl::move(other_);
    }
    if (!socket)
        return;

    socket->OnPeerZeroHandles();
}

void SocketDispatcher::OnPeerZeroHandles() {
    canary_.Assert();

    AutoLock lock(&lock_);
    other_.reset();
    state_tracker_.UpdateState(MX_SOCKET_WRITABLE, MX_SOCKET_PEER_CLOSED);
    if (iopc_)
        iopc_->Signal(MX_SOCKET_PEER_CLOSED, &lock_);
}

status_t SocketDispatcher::user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) {
    canary_.Assert();

    if ((set_mask & ~MX_USER_SIGNAL_ALL) || (clear_mask & ~MX_USER_SIGNAL_ALL))
        return ERR_INVALID_ARGS;

    if (!peer) {
        state_tracker_.UpdateState(clear_mask, set_mask);
        return NO_ERROR;
    }

    mxtl::RefPtr<SocketDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_)
            return ERR_PEER_CLOSED;
        other = other_;
    }

    return other->UserSignalSelf(clear_mask, set_mask);
}

status_t SocketDispatcher::UserSignalSelf(uint32_t clear_mask, uint32_t set_mask) {
    canary_.Assert();

    AutoLock lock(&lock_);
    auto satisfied = state_tracker_.GetSignalsState();
    auto changed = ~satisfied & set_mask;

    if (changed) {
        if (iopc_)
            iopc_->Signal(changed, 0u, &lock_);
    }

    state_tracker_.UpdateState(clear_mask, set_mask);
    return NO_ERROR;
}

status_t SocketDispatcher::set_port_client(mxtl::unique_ptr<PortClient> client) {
    canary_.Assert();

    if ((client->get_trigger_signals() & ~kValidSignalMask) != 0)
        return ERR_INVALID_ARGS;

    AutoLock lock(&lock_);
    if (iopc_)
        return ERR_BAD_STATE;

    iopc_ = mxtl::move(client);

    if (!cbuf_.empty())
        iopc_->Signal(MX_SOCKET_READABLE, 0u, &lock_);

    return NO_ERROR;
}

status_t SocketDispatcher::HalfClose() {
    canary_.Assert();

    mxtl::RefPtr<SocketDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (half_closed_[0])
            return NO_ERROR;
        if (!other_)
            return ERR_PEER_CLOSED;
        other = other_;
        half_closed_[0] = true;
        state_tracker_.UpdateState(MX_SOCKET_WRITABLE, 0u);
    }
    return other->HalfCloseOther();
}

status_t SocketDispatcher::HalfCloseOther() {
    canary_.Assert();

    AutoLock lock(&lock_);
    half_closed_[1] = true;
    state_tracker_.UpdateState(0u, MX_SOCKET_PEER_CLOSED);
    return NO_ERROR;
}

mx_status_t SocketDispatcher::Write(const void* src, size_t len,
                                    bool from_user, size_t* nwritten) {
    canary_.Assert();

    mxtl::RefPtr<SocketDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_)
            return ERR_PEER_CLOSED;
        if (half_closed_[0])
            return ERR_BAD_STATE;
        other = other_;
    }

    return other->WriteSelf(src, len, from_user, nwritten);
}

mx_status_t SocketDispatcher::WriteSelf(const void* src, size_t len,
                                        bool from_user, size_t* written) {
    canary_.Assert();

    AutoLock lock(&lock_);

    if (!cbuf_.free())
        return ERR_SHOULD_WAIT;

    bool was_empty = cbuf_.empty();

    auto st = cbuf_.Write(src, len, from_user);

    if (st > 0) {
        if (was_empty)
            state_tracker_.UpdateState(0u, MX_SOCKET_READABLE);
        if (iopc_)
            iopc_->Signal(MX_SOCKET_READABLE, st, &lock_);
    }

    if (!cbuf_.free())
        other_->state_tracker_.UpdateState(MX_SOCKET_WRITABLE, 0u);

    *written = st;
    return NO_ERROR;
}

mx_status_t SocketDispatcher::Read(void* dest, size_t len,
                                   bool from_user, size_t* nread) {
    canary_.Assert();

    AutoLock lock(&lock_);

    // Just query for bytes outstanding.
    if (!dest && len == 0) {
        *nread = cbuf_.CouldRead();
        return NO_ERROR;
    }

    bool closed = half_closed_[1] || !other_;

    if (cbuf_.empty())
        return closed ? ERR_PEER_CLOSED: ERR_SHOULD_WAIT;

    bool was_full = cbuf_.free() == 0u;

    auto st = cbuf_.Read(dest, len, from_user);

    if (cbuf_.empty()) {
        state_tracker_.UpdateState(MX_SOCKET_READABLE, 0u);
    }

    if (!closed && was_full && (st > 0))
        other_->state_tracker_.UpdateState(0u, MX_SOCKET_WRITABLE);

    *nread = static_cast<size_t>(st);
    return NO_ERROR;
}
