// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/launchpad.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unittest/unittest.h>

#include "private.h"

int thread_injection_test(void) {
    BEGIN_TEST;

    // Create a channel to communicate with the injector.  This channel
    // will serve two purposes.  First, we'll use it to give the
    // injector some important bits and our process handle.  Second,
    // it will serve as the bootstrap channel for the injected program.
    // There is no facility for the injector to inject a handle into
    // another process, so it relies on us (the injectee) having
    // created the channel beforehand and told the injector its handle
    // number in this process.
    mx_handle_t injector_channel_handle, injector_channel;
    mx_status_t status = mx_channel_create(0, &injector_channel,
                                           &injector_channel_handle);
    char msg[128];
    snprintf(msg, sizeof(msg), "mx_channel_create failed: %d", status);
    ASSERT_EQ(status, 0, msg);

    // Now send our own process handle to the injector, along with
    // some crucial information.  This has to be done before starting
    // the injector, so it can immediately read from the channel.
    atomic_int my_futex = ATOMIC_VAR_INIT(0);
    struct helper_data data = {
        .futex_addr = &my_futex,
        .bootstrap = injector_channel,
    };
    mx_handle_t handles[2];

    status = mx_handle_duplicate(mx_process_self(), MX_RIGHT_SAME_RIGHTS,
                                 &handles[0]);
    snprintf(msg, sizeof(msg), "mx_handle_duplicate failed on %#x: %d",
             mx_process_self(), status);
    ASSERT_EQ(status, 0, msg);

    status = mx_handle_duplicate(mx_vmar_root_self(), MX_RIGHT_SAME_RIGHTS,
                                 &handles[1]);
    snprintf(msg, sizeof(msg), "mx_handle_duplicate failed on %#x: %d",
             mx_vmar_root_self(), status);
    ASSERT_EQ(status, 0, msg);

    status = mx_channel_write(injector_channel, 0, &data, sizeof(data),
                              handles, countof(handles));
    snprintf(msg, sizeof(msg), "mx_channel_write failed: %d", status);
    ASSERT_EQ(status, 0, msg);

    // Start the injector program, which will inject a third program
    // into this here process.
    const char* argv[] = { "/boot/bin/thread-injection-injector" };
    uint32_t id = PA_HND(PA_USER0, 0);

    launchpad_t* lp;
    launchpad_create(0, argv[0], &lp);
    launchpad_load_from_file(lp, argv[0]);
    launchpad_set_args(lp, 1, argv);
    launchpad_add_handle(lp, injector_channel_handle, id);
    launchpad_clone(lp, LP_CLONE_ALL);

    mx_handle_t proc;
    const char* errmsg;
    status = launchpad_go(lp, &proc, &errmsg);
    snprintf(msg, sizeof(msg), "launchpad_go failed: %s: %d", errmsg, status);
    ASSERT_GT(proc, 0, msg);
    mx_handle_close(proc);

    // Now the injector will inject the "injected" program into this process.
    // When that program starts up, it will see the &my_futex value and
    // do a store of the magic value and a mx_futex_wake operation.
    // When it's done that, the test has succeeded.
    while (atomic_load(&my_futex) == 0) {
        status = mx_futex_wait(&my_futex, 0, MX_TIME_INFINITE);
        snprintf(msg, sizeof(msg), "mx_futex_wait failed: %d", status);
        ASSERT_EQ(status, 0, msg);
    }
    snprintf(msg, sizeof(msg), "futex set to %#x", my_futex);
    ASSERT_EQ(my_futex, MAGIC, msg);

    END_TEST;
}

BEGIN_TEST_CASE(thread_injection_tests)
RUN_TEST(thread_injection_test)
END_TEST_CASE(thread_injection_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
