#include "paper-football.h"

#include <stdio.h>

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

void debug_trap()
{
    printf("Debug trap!\n");
}



#ifdef MAKE_CHECK

#include "insider.h"

#define N 4
#define GRANULARITY 64

void test_fail(const char * const fmt, ...) __attribute__ ((format (printf, 1, 2)));

int test_multialloc(void)
{
    const char * base = "0aAK";

    static const size_t sizes[N] = {8000, 301, 5002, 503 };
    void * ptrs[N];
    void * const data = multialloc(N, sizes, ptrs, GRANULARITY);

    if (data == NULL) {
        test_fail("Not enought memory to allocate multiblock.");
    }

    /* Fill data */
    for (int i=0; i<N; ++i) {
        char * restrict const ptr = ptrs[i];
        for (size_t j=0; j<sizes[i]; ++j) {
            ptr[j] = base[i] + (j % 10);
        }
    }

    /* Check allligment */
    for (int i=0; i<N; ++i) {
        const void * const ptr = ptrs[i];
        const ptrdiff_t address = ptr_diff(NULL, ptr);
        if (address % GRANULARITY != 0) {
            test_fail("Returned pointer %p is not alligned to %d (index %d).", ptr, GRANULARITY, i);
        }
    }

    /* Check data (overlapping, out of malloc with valgrind) */
    for (int i=0; i<N; ++i) {
        const char * const ptr = ptrs[i];
        for (size_t j=0; j<sizes[i]; ++j) {
            char expected = base[i] + (j % 10);
            if (ptr[j] != expected) {
                test_fail("Unexpected character “%c” in block #%d:\n"
                          "Expected value is “%c”.",
                          ptr[j], i, expected);
            }
        }
    }

    free(data);
    return 0;
}

#endif
