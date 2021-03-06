#if defined(__x86_64__)
#include "x86_64/shm.h"
#elif defined(__aarch64__)
#include "aarch64/shm.h"
#elif defined(__riscv) && __riscv_xlen == 64
#include "riscv64/shm.h"
#else
#error Unsupported architecture!
#endif
