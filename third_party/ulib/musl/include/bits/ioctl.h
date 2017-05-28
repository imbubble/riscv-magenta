#if defined(__x86_64__)
#include "x86_64/ioctl.h"
#elif defined(__aarch64__)
#include "aarch64/ioctl.h"
#elif defined(__riscv) && __riscv_xlen == 64
#include "generic/ioctl.h"
#else
#error Unsupported architecture!
#endif
