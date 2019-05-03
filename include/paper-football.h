#ifndef YOO__PAPER_FOOTBALL__H__
#define YOO__PAPER_FOOTBALL__H__

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>

void * multialloc(
    const size_t n,
    const size_t * const sizes,
    void * restrict * ptrs,
    const size_t granularity);

#endif
