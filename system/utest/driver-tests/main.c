// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <threads.h>

#include <magenta/syscalls.h>
#include <magenta/device/device.h>
#include <magenta/device/test.h>

static const char* test_drivers[] = {
    "iotxn-test",
};

#define DEV_TEST "/dev/misc/test"

static void do_one_test(int tfd, int i, mx_handle_t output) {
    char devpath[1024];
    ssize_t rc = ioctl_test_create_device(tfd, test_drivers[i], strlen(test_drivers[i]) + 1, devpath, sizeof(devpath));
    if (rc < 0) {
        printf("driver-tests: error %zd creating device for %s\n", rc, test_drivers[i]);
    }

    // TODO some waiting needed before opening..,
    usleep(1000);

    int fd;
    int retry = 0;
    do {
        fd = open(devpath, O_RDWR);
        if (fd >= 0) {
            break;
        }
        usleep(1000);
    } while (++retry < 100);

    if (retry == 100) {
        printf("driver-tests: failed to open %s\n", devpath);
        return;
    }

    ioctl_device_bind(fd, test_drivers[i], strlen(test_drivers[i]) + 1);

    mx_handle_t h;
    mx_status_t status = mx_handle_duplicate(output, MX_RIGHT_SAME_RIGHTS, &h);
    if (status != NO_ERROR) {
        printf("driver-tests: error %d duplicating output socket\n", status);
        ioctl_test_destroy_device(fd);
        close(fd);
        return;
    }

    ioctl_test_set_output_socket(fd, &h);

    test_ioctl_test_report_t report;
    ioctl_test_run_tests(fd, NULL, 0, &report);

    ioctl_test_destroy_device(fd);
    close(fd);
}

static int output_thread(void* arg) {
    mx_handle_t h = *(mx_handle_t*)arg;
    char buf[1024];
    for (;;) {
        mx_status_t status = mx_object_wait_one(h, MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED, MX_TIME_INFINITE, NULL);
        if (status != NO_ERROR) {
            break;
        }
        size_t bytes = 0;
        status = mx_socket_read(h, 0u, buf, sizeof(buf), &bytes);
        if (status != NO_ERROR) {
            break;
        }
        size_t written = 0;
        while (written < bytes) {
            ssize_t rc = write(2, buf + written, bytes - written);
            if (rc < 0) {
                break;
            }
            written += rc;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    mx_handle_t socket[2];
    mx_status_t status = mx_socket_create(0u, socket, socket + 1);
    if (status != NO_ERROR) {
        printf("driver-tests: error creating socket\n");
        return -1;
    }

    int fd = open(DEV_TEST, O_RDWR);
    if (fd < 0) {
        printf("driver-tests: no %s device found\n", DEV_TEST);
        return -1;
    }

    thrd_t t;
    int rc = thrd_create_with_name(&t, output_thread, socket, "driver-test-output");
    if (rc != thrd_success) {
        printf("driver-tests: error %d creating output thread\n", rc);
        close(fd);
        mx_handle_close(socket[0]);
        mx_handle_close(socket[1]);
        return -1;
    }

    // bind test drivers
    for (unsigned i = 0; i < sizeof(test_drivers)/sizeof(char*); i++) {
        do_one_test(fd, i, socket[1]);
    }
    close(fd);

    // close this handle before thrd_join to get PEER_CLOSED in output thread
    mx_handle_close(socket[1]);

    thrd_join(t, NULL);
    mx_handle_close(socket[0]);

    return 0;
}
