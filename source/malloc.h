#include <stdlib.h>

void* aligned_alloc(size_t align, size_t size);

static inline void* memalign32(size_t size) {
    return aligned_alloc(0x20, __builtin_align_up(size, 0x20));
}
