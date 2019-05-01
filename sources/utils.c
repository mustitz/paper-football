#include "paper-football.h"

static inline ptrdiff_t ptr_diff(const void * const a, const void * const b)
{
    const char * const byte_ptr_a = a;
    const char * const byte_ptr_b = b;
    return byte_ptr_b - byte_ptr_a;
}

static inline void * ptr_move(void * const ptr, const ptrdiff_t delta)
{
    char * const byte_ptr = ptr;
    return byte_ptr + delta;
}

void * multialloc(const size_t n, const size_t * const sizes,
    void * restrict * ptrs, const size_t granularity)
{
    if (n == 0) {
        return NULL;
    }

    size_t offsets[n];
    size_t offset = 0;
    for (size_t i=0;;) {
        offsets[i] = offset;
        offset += sizes[i];
        if (++i == n) break;

        const size_t mod = offset % granularity;
        if (mod != 0) {
            offset += granularity - mod;
        }
    }

    void * result = malloc(offset + granularity);

    const ptrdiff_t address = ptr_diff(NULL, result);
    const ptrdiff_t mod = address % granularity;
    const size_t gap = mod == 0 ? 0 : granularity - mod;

    for (size_t i=0; i<n; ++i) {
        ptrs[i] = ptr_move(result, offsets[i] + gap);
    }

    return result;
}
