// Copyright 2016 The Fuchsia Authors
// Copyright 2017 Slava Imameev
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/vm.h>
#include <arch/riscv/mmu.h>
#include <platform.h>
#include <memory.h>

struct mmu_initial_mapping mmu_initial_mappings[] = {

    /* 64GB of memory mapped where the kernel lives */
    {
        .phys = MEMBASE,
        .virt = KERNEL_ASPACE_BASE,
        .size = 64ULL*GB, /* maps first 64GB by default */
        .flags = 0,
        .name = "memory"
    },
    /* null entry to terminate the list */
    {}
};


void platform_early_init(void)
{
    /* initialize physical memory arenas */
    platform_mem_init();
}