#ifndef YOO__PAPER_FOOTBALL__H__
#define YOO__PAPER_FOOTBALL__H__

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

void * multialloc(
    const size_t n,
    const size_t * const sizes,
    void * restrict * ptrs,
    const size_t granularity);



#define GOAL_1   -1
#define GOAL_2   -2
#define NO_WAY   -3

enum step {
    NORTH_WEST = 0,
    NORTH,
    NORTH_EAST,
    EAST,
    SOUTH_EAST,
    SOUTH,
    SOUTH_WEST,
    WEST,
    QSTEPS
};



struct geometry
{
    uint32_t qpoints;
    const int32_t * connections;
};

struct geometry * create_std_geometry(
    const int width,
    const int height,
    const int goal_width);

void destroy_geometry(struct geometry * restrict const me);

#endif
