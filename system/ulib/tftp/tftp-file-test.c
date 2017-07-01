// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <tftp/tftp.h>
#include <unittest/unittest.h>

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// This test simulates a tftp file transfer by running two threads. Both the
// file and transport interfaces are implemented in memory buffers.

#define FILE_SIZE 1000000
#define XFER_BLOCK_SIZE 1000
#define XFER_WINDOW_SIZE 20

uint8_t src_file[FILE_SIZE];
uint8_t dst_file[FILE_SIZE];

/* FAUX FILES INTERFACE */

typedef struct {
    uint8_t* buf;
    char filename[PATH_MAX + 1];
} file_info_t;

// Allocate our src and dst buffers, filling both with random values.
void initialize_files(void) {
    int* src_as_ints = (int*)src_file;
    int* dst_as_ints = (int*)dst_file;

    size_t ndx;
    srand(0);
    for (ndx = 0; ndx < FILE_SIZE / sizeof(int); ndx++) {
        src_as_ints[ndx] = rand();
        dst_as_ints[ndx] = rand();
    }
    for (ndx = (FILE_SIZE / sizeof(int)) * sizeof(int);
         ndx < FILE_SIZE;
         ndx++) {
        src_file[ndx] = rand();
        dst_file[ndx] = rand();
    }
}

int compare_files(void) {
    return memcmp(src_file, dst_file, FILE_SIZE);
}

const char* file_get_filename(file_info_t* file_info) {
    return file_info->filename;
}

ssize_t file_open_read(const char* filename, void* file_cookie) {
    file_info_t* file_info = file_cookie;
    file_info->buf = src_file;
    strncpy(file_info->filename, filename, PATH_MAX);
    file_info->filename[PATH_MAX] = '\0';
    return FILE_SIZE;
}

tftp_status file_open_write(const char* filename,
                            size_t size,
                            void* file_cookie) {
    file_info_t* file_info = file_cookie;
    file_info->buf = dst_file;
    file_info->filename[PATH_MAX] = '\0';
    strncpy(file_info->filename, filename, PATH_MAX);
    return TFTP_NO_ERROR;
}

tftp_status file_read(void* data, size_t* length, off_t offset,
                      void* file_cookie) {
    file_info_t* file_info = file_cookie;
    if (offset > FILE_SIZE) {
        // Something has gone wrong in libtftp
        return TFTP_ERR_INTERNAL;
    }
    if ((offset + *length) > FILE_SIZE) {
        *length = FILE_SIZE - offset;
    }
    memcpy(data, &file_info->buf[offset], *length);
    return TFTP_NO_ERROR;
}

tftp_status file_write(const void* data, size_t* length, off_t offset,
                       void* file_cookie) {
    file_info_t* file_info = file_cookie;
    if ((offset >= FILE_SIZE) || ((offset + *length) > FILE_SIZE)) {
        // Something has gone wrong in libtftp
        return TFTP_ERR_INTERNAL;
    }
    memcpy(&file_info->buf[offset], data, *length);
    return TFTP_NO_ERROR;
}

void file_close(void* file_cookie) {
}

/* FAUX SOCKET INTERFACE */

#define FAKE_SOCK_BUF_SZ 65536
typedef struct {
    uint8_t buf[FAKE_SOCK_BUF_SZ];
    size_t size;
    _Atomic size_t read_ndx;
    _Atomic size_t write_ndx;
} fake_socket_t;
fake_socket_t client_out_socket = { .size = FAKE_SOCK_BUF_SZ };
fake_socket_t server_out_socket = { .size = FAKE_SOCK_BUF_SZ };

typedef struct {
    fake_socket_t* in_sock;
    fake_socket_t* out_sock;
} transport_info_t;

// Initialize "sockets" for either client or server.
void transport_init(transport_info_t* transport_info, bool is_server) {
    if (is_server) {
        transport_info->in_sock = &client_out_socket;
        transport_info->out_sock = &server_out_socket;
    } else {
        transport_info->in_sock = &server_out_socket;
        transport_info->out_sock = &client_out_socket;
    }
    transport_info->in_sock->read_ndx = 0;
    transport_info->out_sock->write_ndx = 0;
}

// Write to our circular message buffer.
void write_to_buf(fake_socket_t* sock, void* data, size_t size) {
    uint8_t* in_buf = data;
    uint8_t* out_buf = sock->buf;
    size_t curr_offset = sock->write_ndx % sock->size;
    if (curr_offset + size <= sock->size) {
        memcpy(&out_buf[curr_offset], in_buf, size);
    } else {
        size_t first_size = sock->size - curr_offset;
        size_t second_size = size - first_size;
        memcpy(out_buf + curr_offset, in_buf, first_size);
        memcpy(out_buf, in_buf + first_size, second_size);
    }
    sock->write_ndx += size;
}

// Send a message. Note that the buffer's read_ndx and write_ndx don't wrap,
// which makes it easier to recognize underflow.
int transport_send(void* data, size_t len, void* transport_cookie) {
    transport_info_t* transport_info = transport_cookie;
    fake_socket_t* sock = transport_info->out_sock;
    while ((sock->write_ndx + sizeof(len) + len - sock->read_ndx)
           > sock->size) {
        // Wait for the other thread to catch up
        usleep(10);
    }
    write_to_buf(sock, &len, sizeof(len));
    write_to_buf(sock, data, len);
    return 0;
}

// Read from our circular message buffer. If |move_ptr| is false, just peeks at
// the data (reads without updating the read pointer).
void read_from_buf(fake_socket_t* sock, void* data, size_t size,
                   bool move_ptr) {
    uint8_t* in_buf = sock->buf;
    uint8_t* out_buf = data;
    size_t curr_offset = sock->read_ndx % sock->size;
    if (curr_offset + size <= sock->size) {
        memcpy(out_buf, &in_buf[curr_offset], size);
    } else {
        size_t first_size = sock->size - curr_offset;
        size_t second_size = size - first_size;
        memcpy(out_buf, in_buf + curr_offset, first_size);
        memcpy(out_buf + first_size, in_buf, second_size);
    }
    if (move_ptr) {
        sock->read_ndx += size;
    }
}

// Receive a message. Note that the buffer's read_ndx and write_ndx don't
// wrap, which makes it easier to recognize underflow.
int transport_recv(void* data, size_t len, bool block, void* transport_cookie) {
    transport_info_t* transport_info = transport_cookie;
    if (block) {
        while ((transport_info->in_sock->read_ndx + sizeof(size_t)) >=
               transport_info->in_sock->write_ndx) {
            usleep(10);
        }
    } else if ((transport_info->in_sock->read_ndx + sizeof(size_t)) >=
               transport_info->in_sock->write_ndx) {
        return TFTP_ERR_TIMED_OUT;
    }
    size_t block_len;
    read_from_buf(transport_info->in_sock, &block_len, sizeof(block_len),
                  false);
    if (block_len > len) {
        return TFTP_ERR_BUFFER_TOO_SMALL;
    }
    transport_info->in_sock->read_ndx += sizeof(block_len);
    read_from_buf(transport_info->in_sock, data, block_len, true);
    return block_len;
}

int transport_timeout_set(uint32_t timeout_ms, void* transport_cookie) {
    return 0;
}

/// SEND THREAD

bool run_send_test(void) {
    BEGIN_HELPER;

    // Configure TFTP session
    tftp_session* session;
    size_t session_size = tftp_sizeof_session();
    void* session_buf = malloc(session_size);
    ASSERT_NEQ(session_buf, NULL, "memory allocation failed");

    tftp_status status = tftp_init(&session, session_buf, session_size);
    ASSERT_EQ(status, TFTP_NO_ERROR, "unable to initialize a tftp session");

    // Configure file interface
    file_info_t file_info;
    tftp_file_interface file_callbacks = { file_open_read,
                                           file_open_write,
                                           file_read,
                                           file_write,
                                           file_close };
    status = tftp_session_set_file_interface(session, &file_callbacks);
    ASSERT_EQ(status, TFTP_NO_ERROR, "could not set file interface");

    // Configure transport interface
    transport_info_t transport_info;
    transport_init(&transport_info, false);

    tftp_transport_interface transport_callbacks = { transport_send,
                                                     transport_recv,
                                                     transport_timeout_set };
    status = tftp_session_set_transport_interface(session,
                                                  &transport_callbacks);
    ASSERT_EQ(status, TFTP_NO_ERROR, "could not set transport interface");

    // Allocate intermediate buffers
    size_t buf_sz = XFER_BLOCK_SIZE > PATH_MAX ?
                    XFER_BLOCK_SIZE + 2 : PATH_MAX + 2;
    char* msg_in_buf = malloc(buf_sz);
    ASSERT_NEQ(msg_in_buf, NULL, "memory allocation failure");
    char* msg_out_buf = malloc(buf_sz);
    ASSERT_NEQ(msg_out_buf, NULL, "memory allocation failure");

    char err_msg_buf[128];
    size_t block_sz = XFER_BLOCK_SIZE;
    uint8_t window_sz = XFER_WINDOW_SIZE;

    tftp_request_opts opts = { .inbuf = msg_in_buf,
                               .inbuf_sz = buf_sz,
                               .outbuf = msg_out_buf,
                               .outbuf_sz = buf_sz,
                               .block_size = &block_sz,
                               .window_size = &window_sz,
                               .err_msg = err_msg_buf,
                               .err_msg_sz = sizeof(err_msg_buf) };
    status = tftp_push_file(session, &transport_info, &file_info, "abc.txt",
                            "xyz.txt", &opts);
    EXPECT_GE(status, 0, "failed to send file");
done:
    free(session);
    END_HELPER;
}

void* tftp_send_main(void* arg) {
    run_send_test();
    pthread_exit(NULL);
}

/// RECV THREAD

bool run_recv_test(void) {
    BEGIN_HELPER;

    // Configure TFTP session
    tftp_session* session;
    size_t session_size = tftp_sizeof_session();
    void* session_buf = malloc(session_size);
    ASSERT_NEQ(session_buf, NULL, "memory allocation failed");

    tftp_status status = tftp_init(&session, session_buf, session_size);
    ASSERT_EQ(status, TFTP_NO_ERROR, "unable to initiate a tftp session");

    // Configure file interface
    file_info_t file_info;
    tftp_file_interface file_callbacks = { file_open_read,
                                           file_open_write,
                                           file_read,
                                           file_write,
                                           file_close };
    status = tftp_session_set_file_interface(session, &file_callbacks);
    ASSERT_EQ(status, TFTP_NO_ERROR, "could not set file interface");

    // Configure transport interface
    transport_info_t transport_info;
    transport_init(&transport_info, true);
    tftp_transport_interface transport_callbacks = { transport_send,
                                                     transport_recv,
                                                     transport_timeout_set };
    status = tftp_session_set_transport_interface(session,
                                                  &transport_callbacks);
    ASSERT_EQ(status, TFTP_NO_ERROR, "could not set transport interface");

    // Allocate intermediate buffers
    size_t buf_sz = XFER_BLOCK_SIZE > PATH_MAX ?
                    XFER_BLOCK_SIZE + 2 : PATH_MAX + 2;
    char* msg_in_buf = malloc(buf_sz);
    ASSERT_NEQ(msg_in_buf, NULL, "memory allocation failure");
    char* msg_out_buf = malloc(buf_sz);
    ASSERT_NEQ(msg_out_buf, NULL, "memory allocation failure");

    char err_msg_buf[128];
    tftp_handler_opts opts = { .inbuf = msg_in_buf,
                               .inbuf_sz = buf_sz,
                               .outbuf = msg_out_buf,
                               .outbuf_sz = buf_sz,
                               .err_msg = err_msg_buf,
                               .err_msg_sz = sizeof(err_msg_buf) };
    do {
        status = tftp_handle_request(session, &transport_info, &file_info,
                                     &opts);
    } while (status == TFTP_NO_ERROR);
    EXPECT_EQ(status, TFTP_TRANSFER_COMPLETED, "failed to receive file");
done:
    free(session);
    END_HELPER;
}

void* tftp_recv_main(void* arg) {
    run_recv_test();
    pthread_exit(NULL);
}

bool test_tftp_send_file(void) {
    BEGIN_TEST;
    initialize_files();

    pthread_t send_thread, recv_thread;
    pthread_create(&send_thread, NULL, tftp_send_main, NULL);
    pthread_create(&recv_thread, NULL, tftp_recv_main, NULL);

    pthread_join(send_thread, NULL);
    pthread_join(recv_thread, NULL);

    int compare_result = compare_files();
    EXPECT_EQ(compare_result, 0, "output file mismatch");

    END_TEST;
}

BEGIN_TEST_CASE(tftp_send_file)
RUN_TEST(test_tftp_send_file)
END_TEST_CASE(tftp_send_file)

