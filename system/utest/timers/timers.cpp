// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <threads.h>

#include <mx/channel.h>
#include <mx/event.h>
#include <mx/handle.h>
#include <mx/port.h>

#include <mxtl/type_support.h>

#include <unistd.h>
#include <unittest/unittest.h>

static bool basic_test() {
    BEGIN_TEST;
    mx::handle timer;
    ASSERT_EQ(mx_timer_create(0, timer.get_address()), NO_ERROR, "");

    mx_signals_t pending;
    EXPECT_EQ(timer.wait_one(MX_TIMER_SIGNALED, 0u, &pending), ERR_TIMED_OUT, "");
    EXPECT_EQ(pending, MX_SIGNAL_LAST_HANDLE, "");

    for (int ix = 0; ix != 10; ++ix) {
        const auto deadline_timer = mx_deadline_after(MX_MSEC(50));
        const auto deadline_wait = mx_deadline_after(MX_SEC(1));
        // Timer should fire first than the wait.
        ASSERT_EQ(mx_timer_start(timer.get(), deadline_timer, 0u, 0u), NO_ERROR, "");
        EXPECT_EQ(timer.wait_one(MX_TIMER_SIGNALED, deadline_wait, &pending), NO_ERROR, "");
        EXPECT_EQ(pending, MX_TIMER_SIGNALED | MX_SIGNAL_LAST_HANDLE, "");
    }
    END_TEST;
}

static bool restart_test() {
    BEGIN_TEST;
    mx::handle timer;
    ASSERT_EQ(mx_timer_create(0, timer.get_address()), NO_ERROR, "");

    mx_signals_t pending;
    for (int ix = 0; ix != 10; ++ix) {
        const auto deadline_timer = mx_deadline_after(MX_MSEC(500));
        const auto deadline_wait = mx_deadline_after(MX_MSEC(1));
        // Setting a timer already running is equivalent to a cancel + set.
        ASSERT_EQ(mx_timer_start(timer.get(), deadline_timer, 0u, 0u), NO_ERROR, "");
        EXPECT_EQ(timer.wait_one(MX_TIMER_SIGNALED, deadline_wait, &pending), ERR_TIMED_OUT, "");
        EXPECT_EQ(pending, MX_SIGNAL_LAST_HANDLE, "");
    }
    END_TEST;
}

BEGIN_TEST_CASE(timers_test)
RUN_TEST(basic_test)
RUN_TEST(restart_test)
END_TEST_CASE(timers_test)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
