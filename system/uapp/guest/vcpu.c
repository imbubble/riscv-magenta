// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <hypervisor/acpi.h>
#include <hypervisor/decode.h>
#include <hypervisor/ports.h>
#include <magenta/assert.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

#if __x86_64__
#include <acpica/acpi.h>
#include <acpica/actypes.h>
#endif // __x86_64__

#include "vcpu.h"

/* Memory-mapped device physical addresses. */
#define LOCAL_APIC_PHYS_BASE                0xfee00000
#define LOCAL_APIC_PHYS_TOP                 (LOCAL_APIC_PHYS_BASE + PAGE_SIZE - 1)
#define IO_APIC_PHYS_BASE                   0xfec00000
#define IO_APIC_PHYS_TOP                    (IO_APIC_PHYS_BASE + PAGE_SIZE - 1)
#define TPM_PHYS_BASE                       0xfed40000
#define TPM_PHYS_TOP                        (TPM_PHYS_BASE + 0x5000 - 1)

/* Local APIC register addresses. */
#define LOCAL_APIC_REGISTER_ID              0x0020
#define LOCAL_APIC_REGISTER_EOI             0x00b0
#define LOCAL_APIC_REGISTER_SVR             0x00f0
#define LOCAL_APIC_REGISTER_ESR             0x0280
#define LOCAL_APIC_REGISTER_LVT_TIMER       0x0320
#define LOCAL_APIC_REGISTER_LVT_ERROR       0x0370
#define LOCAL_APIC_REGISTER_INITIAL_COUNT   0x0380

/* IO APIC register addresses. */
#define IO_APIC_IOREGSEL                    0x00
#define IO_APIC_IOWIN                       0x10

/* IO APIC register names. */
#define IO_APIC_REGISTER_ID                 0x00
#define IO_APIC_REGISTER_VER                0x01

/* IO APIC configuration constants. */
#define IO_APIC_VERSION                     0x11
#define FIRST_REDIRECT_OFFSET               0x10
#define LAST_REDIRECT_OFFSET                (FIRST_REDIRECT_OFFSET + IO_APIC_REDIRECT_OFFSETS - 1)

/* TPM register names. */
#define TPM_REGISTER_ACCESS                 0x00

/* UART configuration flags. */
#define UART_STATUS_IDLE                    (1u << 6)

/* RTC registers. */
#define RTC_REGISTER_SECONDS                0u
#define RTC_REGISTER_MINUTES                2u
#define RTC_REGISTER_HOURS                  4u
#define RTC_REGISTER_DAY_OF_MONTH           7u
#define RTC_REGISTER_MONTH                  8u
#define RTC_REGISTER_YEAR                   9u
#define RTC_REGISTER_A                      10u
#define RTC_REGISTER_B                      11u
#define RTC_REGISTER_C                      12u
#define RTC_REGISTER_D                      13u

/* RTC register B flags. */
#define RTC_REGISTER_B_DAYLIGHT_SAVINGS     (1u << 0)
#define RTC_REGISTER_B_HOUR_FORMAT          (1u << 1)
#define RTC_REGISTER_B_DATA_MODE            (1u << 2)

/* I8042 status flags. */
#define I8042_STATUS_OUTPUT_FULL            (1u << 0)
#define I8042_STATUS_INPUT_FULL             (1u << 1)

/* I8042 test constants. */
#define I8042_COMMAND_TEST                  0xaa
#define I8042_DATA_TEST_RESPONSE            0x55

/* PM registers */
#define PM1A_REGISTER_ENABLE                (ACPI_PM1_REGISTER_WIDTH / 8)

mx_status_t handle_rtc(uint8_t rtc_index, uint8_t* value) {
    time_t now = time(NULL);
    struct tm tm;
    if (localtime_r(&now, &tm) == NULL)
        return ERR_INTERNAL;
    switch (rtc_index) {
    case RTC_REGISTER_SECONDS:
        *value = tm.tm_sec;
        break;
    case RTC_REGISTER_MINUTES:
        *value = tm.tm_min;
        break;
    case RTC_REGISTER_HOURS:
        *value = tm.tm_hour;
        break;
    case RTC_REGISTER_DAY_OF_MONTH:
        *value = tm.tm_mday;
        break;
    case RTC_REGISTER_MONTH:
        *value = tm.tm_mon;
        break;
    case RTC_REGISTER_YEAR:
        // RTC expects the number of years since 2000.
        *value = tm.tm_year - 100;
        break;
    case RTC_REGISTER_B:
        *value = RTC_REGISTER_B_HOUR_FORMAT | RTC_REGISTER_B_DATA_MODE;
        if (tm.tm_isdst)
            *value |= RTC_REGISTER_B_DAYLIGHT_SAVINGS;
        break;
    default:
        return ERR_NOT_SUPPORTED;
    }
    return NO_ERROR;
}

static mx_status_t handle_port_in(vcpu_context_t* context, const mx_guest_port_in_t* port_in) {
    uint8_t input_size = 1;
    mx_guest_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = MX_GUEST_PKT_TYPE_PORT_IN;

    io_port_state_t* io_port_state = &context->guest_state->io_port_state;
    switch (port_in->port) {
    default:
        return ERR_NOT_SUPPORTED;
    case UART_RECEIVE_IO_PORT + 4:
        packet.port_in_ret.u8 = 0;
        break;
    case UART_STATUS_IO_PORT:
        packet.port_in_ret.u8 = UART_STATUS_IDLE;
        break;
    case RTC_DATA_PORT: {
        mx_status_t status = handle_rtc(io_port_state->rtc_index, &packet.port_in_ret.u8);
        if (status != NO_ERROR)
            return status;
        break;
    }
    case I8042_DATA_PORT:
        if (io_port_state->i8042_command == I8042_COMMAND_TEST) {
            packet.port_in_ret.u8 = I8042_DATA_TEST_RESPONSE;
        } else {
            packet.port_in_ret.u8 = 0;
        }
        break;
    case I8042_COMMAND_PORT:
        packet.port_in_ret.u8 = I8042_STATUS_OUTPUT_FULL;
        break;
#if __x86_64__
    case PM1_EVENT_PORT + PM1A_REGISTER_ENABLE:
        input_size = 2;
        packet.port_in_ret.u16 = io_port_state->pm1_enable;
        break;
#endif // __x86_64__
    }

    if (port_in->access_size != input_size)
        return ERR_IO_DATA_INTEGRITY;
    uint32_t num_packets;
    return mx_fifo_write(context->vcpu_fifo, &packet, sizeof(packet), &num_packets);
}

static mx_status_t handle_port_out(vcpu_context_t* context, const mx_guest_port_out_t* port_out) {
    io_port_state_t* io_port_state = &context->guest_state->io_port_state;
    switch (port_out->port) {
    default:
        return ERR_NOT_SUPPORTED;
    case PIC1_PORT ... PIC1_PORT + 1:
    case PIC2_PORT ... PIC2_PORT + 2:
    case I8253_CONTROL_PORT:
    case I8042_DATA_PORT:
    case UART_RECEIVE_IO_PORT + 1 ... UART_RECEIVE_IO_PORT + 5:
        return NO_ERROR;
    case UART_RECEIVE_IO_PORT:
        for (int i = 0; i < port_out->access_size; i++) {
            io_port_state->buffer[io_port_state->offset++] = port_out->data[i];
            if (io_port_state->offset == IO_BUFFER_SIZE || port_out->data[i] == '\r') {
                printf("%.*s", io_port_state->offset, io_port_state->buffer);
                io_port_state->offset = 0;
            }
        }
        return NO_ERROR;
    case RTC_INDEX_PORT:
        if (port_out->access_size != 1)
            return ERR_IO_DATA_INTEGRITY;
        io_port_state->rtc_index = port_out->u8;
        return NO_ERROR;
    case I8042_COMMAND_PORT:
        if (port_out->access_size != 1)
            return ERR_IO_DATA_INTEGRITY;
        io_port_state->i8042_command = port_out->u8;
        return NO_ERROR;
#if __x86_64__
    case PM1_EVENT_PORT + PM1A_REGISTER_ENABLE:
        if (port_out->access_size != 2)
            return ERR_IO_DATA_INTEGRITY;
        io_port_state->pm1_enable = port_out->u16;
        return NO_ERROR;
#endif // __x86_64__
    }
}

static uint32_t get_value(const instruction_t* inst) {
    return inst->reg != NULL ? *inst->reg : inst->imm;
}

static void apply_inst(const instruction_t* inst, uint32_t* value) {
    if (inst->read) {
        *inst->reg = *value;
    } else {
        *value = get_value(inst);
    }
}

static mx_status_t handle_local_apic(vcpu_context_t* context, const mx_guest_mem_trap_t* mem_trap,
                                     instruction_t* inst) {
    MX_ASSERT(mem_trap->guest_paddr >= LOCAL_APIC_PHYS_BASE);
    mx_vaddr_t offset = mem_trap->guest_paddr - LOCAL_APIC_PHYS_BASE;
    switch (offset) {
    case LOCAL_APIC_REGISTER_ID:
        if (!inst->read)
            return ERR_NOT_SUPPORTED;
        *inst->reg = 0;
        return NO_ERROR;
    case LOCAL_APIC_REGISTER_EOI:
        // TODO(abdulla): Correctly handle EOI.
        if (inst->read)
            return ERR_INVALID_ARGS;
        return NO_ERROR;
    case LOCAL_APIC_REGISTER_SVR:
    case LOCAL_APIC_REGISTER_ESR:
    case LOCAL_APIC_REGISTER_LVT_TIMER:
    case LOCAL_APIC_REGISTER_LVT_ERROR: {
        // From Intel Volume 3, Section 10.5.3: Before attempt to read from the
        // ESR, software should first write to it.
        //
        // Therefore, we ignore writes to the ESR.
        if (!inst->read && offset == LOCAL_APIC_REGISTER_ESR)
            return NO_ERROR;
        apply_inst(inst, context->local_apic_state.apic_addr + offset);
        return NO_ERROR;
    }
    case LOCAL_APIC_REGISTER_INITIAL_COUNT:
        if (inst->read || get_value(inst) > 0)
            return ERR_NOT_SUPPORTED;
        return NO_ERROR;
    }
    return ERR_NOT_SUPPORTED;
}

static mx_status_t handle_io_apic(vcpu_context_t* context, const mx_guest_mem_trap_t* mem_trap,
                                  instruction_t* inst) {
    io_apic_state_t* io_apic_state = &context->guest_state->io_apic_state;
    MX_ASSERT(mem_trap->guest_paddr >= IO_APIC_PHYS_BASE);
    mx_vaddr_t offset = mem_trap->guest_paddr - IO_APIC_PHYS_BASE;
    switch (offset) {
    case IO_APIC_IOREGSEL:
        if (inst->read)
            return ERR_NOT_SUPPORTED;
        io_apic_state->select = get_value(inst);
        return io_apic_state->select > UINT8_MAX ? ERR_INVALID_ARGS : NO_ERROR;
    case IO_APIC_IOWIN:
        switch (io_apic_state->select) {
        case IO_APIC_REGISTER_ID:
            apply_inst(inst, &io_apic_state->id);
            return NO_ERROR;
        case IO_APIC_REGISTER_VER:
            if (!inst->read || inst->reg == NULL)
                return ERR_NOT_SUPPORTED;
            // There are two redirect offsets per redirection entry. We return
            // the maximum redirection entry index.
            //
            // From Intel 82093AA, Section 3.2.2.
            *inst->reg = (IO_APIC_REDIRECT_OFFSETS / 2 - 1) << 16 | IO_APIC_VERSION;
            return NO_ERROR;
        case FIRST_REDIRECT_OFFSET ... LAST_REDIRECT_OFFSET: {
            uint32_t i = io_apic_state->select - FIRST_REDIRECT_OFFSET;
            apply_inst(inst, io_apic_state->redirect + i);
            return NO_ERROR;
        }}
    }
    return ERR_NOT_SUPPORTED;
}

static mx_status_t handle_tpm(const mx_guest_mem_trap_t* mem_trap, instruction_t* inst) {
    MX_ASSERT(mem_trap->guest_paddr >= TPM_PHYS_BASE);
    mx_vaddr_t offset = mem_trap->guest_paddr - TPM_PHYS_BASE;
    switch (offset) {
    case TPM_REGISTER_ACCESS:
        if (!inst->read)
            return ERR_NOT_SUPPORTED;
        if (inst->mem != 1u)
            return ERR_BAD_STATE;
        // Respond with all ones to signal an invalid access to device memory.
        // This should effectively disable any TPM driver.
        *inst->reg = UINT8_MAX;
        return NO_ERROR;
    }
    return ERR_NOT_SUPPORTED;
}

static mx_status_t handle_mem_trap(vcpu_context_t* context, const mx_guest_mem_trap_t* mem_trap) {
    mx_guest_gpr_t guest_gpr;
    mx_status_t status = mx_hypervisor_op(context->guest, MX_HYPERVISOR_OP_GUEST_GET_GPR,
                                          NULL, 0, &guest_gpr, sizeof(guest_gpr));
    if (status != NO_ERROR)
        return status;

    instruction_t inst;
#if __aarch64__
    status = ERR_NOT_SUPPORTED;
#elif __x86_64__
    status = decode_instruction(mem_trap->instruction_buffer, mem_trap->instruction_length,
                                &guest_gpr, &inst);
#else
#error Unsupported architecture
#endif

    if (status != NO_ERROR) {
        fprintf(stderr, "Unsupported instruction\n");
    } else {
        status = ERR_UNAVAILABLE;
        switch (mem_trap->guest_paddr) {
        case LOCAL_APIC_PHYS_BASE ... LOCAL_APIC_PHYS_TOP:
            status = handle_local_apic(context, mem_trap, &inst);
            break;
        case IO_APIC_PHYS_BASE ... IO_APIC_PHYS_TOP:
            mtx_lock(&context->guest_state->mutex);
            status = handle_io_apic(context, mem_trap, &inst);
            mtx_unlock(&context->guest_state->mutex);
            break;
        case TPM_PHYS_BASE ... TPM_PHYS_TOP:
            status = handle_tpm(mem_trap, &inst);
            break;
        }
    }

    mx_guest_packet_t packet;
    packet.type = MX_GUEST_PKT_TYPE_MEM_TRAP;
    packet.mem_trap_ret.fault = status != NO_ERROR;

    // If there was an attempt to read from memory, update the GPRs.
    if (status == NO_ERROR && inst.read) {
        status = mx_hypervisor_op(context->guest, MX_HYPERVISOR_OP_GUEST_SET_GPR,
                                  &guest_gpr, sizeof(guest_gpr), NULL, 0);
        if (status != NO_ERROR)
            return status;
    }
    uint32_t num_packets;
    return mx_fifo_write(context->vcpu_fifo, &packet, sizeof(packet), &num_packets);
}

static mx_status_t vcpu_wait(mx_handle_t vcpu_fifo, mx_signals_t signals) {
    mx_signals_t observed = 0;
    while (!(observed & signals)) {
        mx_status_t status = mx_object_wait_one(vcpu_fifo, signals | MX_FIFO_PEER_CLOSED,
                                                MX_TIME_INFINITE, &observed);
        if (status != NO_ERROR)
            return status;
        if (observed & MX_FIFO_PEER_CLOSED)
            return ERR_PEER_CLOSED;
    }
    return NO_ERROR;
}

mx_status_t vcpu_loop(vcpu_context_t* context) {
    mx_guest_packet_t packet[PAGE_SIZE / MX_GUEST_MAX_PKT_SIZE];
    while (true) {
        mx_status_t status = vcpu_wait(context->vcpu_fifo, MX_FIFO_READABLE);
        if (status != NO_ERROR) {
            fprintf(stderr, "Failed to wait for readable on the VCPU: %d\n", status);
            return status;
        }

        uint32_t num_packets;
        status = mx_fifo_read(context->vcpu_fifo, &packet, sizeof(packet), &num_packets);
        if (status != NO_ERROR)
            return status;

        for (uint32_t i = 0; i < num_packets; i++) {
            mx_status_t status;
            switch (packet[i].type) {
            case MX_GUEST_PKT_TYPE_PORT_IN:
                mtx_lock(&context->guest_state->mutex);
                status = handle_port_in(context, &packet[i].port_in);
                mtx_unlock(&context->guest_state->mutex);
                break;
            case MX_GUEST_PKT_TYPE_PORT_OUT:
                mtx_lock(&context->guest_state->mutex);
                status = handle_port_out(context, &packet[i].port_out);
                mtx_unlock(&context->guest_state->mutex);
                break;
            case MX_GUEST_PKT_TYPE_MEM_TRAP:
                status = handle_mem_trap(context, &packet[i].mem_trap);
                break;
            default:
                fprintf(stderr, "Unhandled guest packet %d\n", packet[i].type);
                return ERR_NOT_SUPPORTED;
            }
            if (status != NO_ERROR) {
                fprintf(stderr, "Failed to handle guest packet %d: %d\n", packet[i].type, status);
                return status;
            }
        }
    }
}
