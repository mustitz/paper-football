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

struct state
{
    const struct geometry * geometry;
    uint8_t * lines;
    int active;
    int ball;
};

enum state_status
{
    IN_PROGRESS = 0,
    WIN_1,
    WIN_2
};

void init_lines(
    const struct geometry * const geometry,
    uint8_t * restrict const lines);

struct state * create_state(const struct geometry * const geometry);
void destroy_state(struct state * restrict const me);

enum state_status state_status(const struct state * const me);

#endif
