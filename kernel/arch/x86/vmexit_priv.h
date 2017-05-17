// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

class AutoVmcsLoad;
class FifoDispatcher;
class GuestPhysicalAddressSpace;
struct GuestState;
struct IoApicState;
struct LocalApicState;
struct VmxState;

/* VM exit reasons. */
enum class ExitReason : uint32_t {
    EXTERNAL_INTERRUPT          = 1u,
    INTERRUPT_WINDOW            = 7u,
    CPUID                       = 10u,
    HLT                         = 12u,
    VMCALL                      = 18u,
    IO_INSTRUCTION              = 30u,
    RDMSR                       = 31u,
    WRMSR                       = 32u,
    ENTRY_FAILURE_GUEST_STATE   = 33u,
    ENTRY_FAILURE_MSR_LOADING   = 34u,
    APIC_ACCESS                 = 44u,
    EPT_VIOLATION               = 48u,
    XSETBV                      = 55u,
};

/* Stores VM exit info from VMCS fields. */
struct ExitInfo {
    ExitReason exit_reason;
    uint64_t exit_qualification;
    uint32_t instruction_length;
    uint64_t guest_physical_address;
    uint64_t guest_rip;

    ExitInfo();
};

/* Stores IO instruction info from the VMCS exit qualification field. */
struct IoInfo {
    uint8_t bytes;
    bool input;
    bool string;
    bool repeat;
    uint16_t port;

    IoInfo(uint64_t qualification);
};

/* VM entry interruption type. */
enum class InterruptionType : uint32_t {
    EXTERNAL_INTERRUPT  = 0u,
    HARDWARE_EXCEPTION  = 3u,
};

/* Local APIC registers. */
enum class ApicRegister : uint16_t {
    LOCAL_APIC_ID   = 0x0020,
    EOI             = 0x00b0,
    SVR             = 0x00f0,
    ESR             = 0x0280,
    LVT_TIMER       = 0x0320,
    LVT_ERROR       = 0x0370,
    INITIAL_COUNT   = 0x0380,
};

/* Stores local APIC access info from the VMCS exit qualification field. */
struct ApicAccessInfo {
    ApicRegister reg;
    uint8_t type;

    ApicAccessInfo(uint64_t qualification);
};

/* Stores info from a decoded instruction. */
struct Instruction {
    bool read;
    bool rex;
    uint32_t imm;
    uint64_t* reg;
};

void interrupt_window_exiting(bool enable);
status_t decode_instruction(const uint8_t* inst_buf, uint32_t inst_len, GuestState* guest_state,
                            Instruction* inst);
status_t vmexit_handler(AutoVmcsLoad* vmcs_load, GuestState* guest_state,
                        LocalApicState* local_apic_state, IoApicState* io_apic_state,
                        GuestPhysicalAddressSpace* gpas, FifoDispatcher* serial_fifo);
