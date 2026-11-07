#include <stdbool.h>
#include <stdint.h>

/* CPU stand-ins for the two AIR atomics used by the synthetic kernels. */
uint32_t cpu_atomic_add(uint32_t *p, uint32_t value, uint32_t order, uint32_t scope, bool is_volatile) {
    (void)order; (void)scope; (void)is_volatile;
    return __atomic_fetch_add(p, value, __ATOMIC_SEQ_CST);
}

uint32_t cpu_atomic_sub(uint32_t *p, uint32_t value, uint32_t order, uint32_t scope, bool is_volatile) {
    (void)order; (void)scope; (void)is_volatile;
    return __atomic_fetch_sub(p, value, __ATOMIC_SEQ_CST);
}
