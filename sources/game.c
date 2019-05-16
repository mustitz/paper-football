#include "paper-football.h"

static inline int check_dim(const int value)
{
    if (value <= 4) {
	    return errno = EINVAL;
    }

    if (value % 2 == 0) {
        return errno = EINVAL;
    }

    return 0;
}

static inline int check_std_arg(const int width, const int height, const int goal_width)
{
    int status;

    status = check_dim(width);
    if (status) {
        return status;
    }

    status = check_dim(height);
    if (status) {
        return status;
    }

    if (goal_width < 2) {
        return errno = EINVAL;
    }

    if (goal_width % 2 != 0) {
        return errno = EINVAL;
    }

    if (goal_width + 3 > width) {
        return errno = EINVAL;
    }

    return 0;
}

static inline int is_valid_move(const int width, const int height, const int goal_width,
    const int x1, const int y1, const int x2, const int y2)
{
    if (x2 > 0 && x2 < width-1 && y1 > 0 && y1 < height-1) return 1;
    if (x2 < 0 || y2 < 0 || x2 >= width || y2 >= height) return 0;
    const int goal1 = (width-goal_width)/2;
    const int goal2 = (width+goal_width)/2;
    if (x1 >= goal1 && x1 <= goal2 && x2 >= goal1 && x2 <= goal2) return 1;
    if (x1 == x2 && (x1 == 0 || x1 == width-1)) return 0;
    if (y1 == y2 && (y1 == 0 || y1 == height-1)) return 0;
    return 1;
}

static inline int goal_status(const int width, const int height, const int goal_width,
    const int x1, const int y1, const int x2, const int y2)
{
    if (y2 != -1 && y2 != height) return NO_WAY;
    const int goal_x1 = (width-goal_width)/2;
    const int goal_x2 = (width+goal_width)/2;
    if (x1 < goal_x1 || x1 > goal_x2) return NO_WAY;
    if (x2 < goal_x1 || x2 > goal_x2) return NO_WAY;
    if (x1 == x2 && (x1 == goal_x1 || x1 == goal_x2)) return NO_WAY;
    return y2 != -1 ? GOAL_1 : GOAL_2;
}

struct geometry * create_std_geometry(const int width, const int height, const int goal_width)
{
    const int status = check_std_arg(width, height, goal_width);
    if (status) {
        return NULL;
    }

    const uint32_t qpoints = (uint32_t)(width) * (uint32_t)(height);
    const size_t board_map_sz = qpoints * QSTEPS * sizeof(uint32_t);
    const size_t sizes[2] = { sizeof(struct geometry), board_map_sz };
    void * ptrs[2];
    void * data = multialloc(2, sizes, ptrs, 256);

    if (data == NULL) {
        return NULL;
    }

    struct geometry * restrict const me = data;

    static const int delta_x[QSTEPS] = { -1,  0, +1, +1, +1,  0, -1, -1 };
    static const int delta_y[QSTEPS] = { +1, +1, +1,  0, -1, -1, -1,  0 };
    int steps[QSTEPS] = { width-1, width, width+1, 1, -width+1, -width, -width-1, -1 };

    int32_t * restrict ptr = ptrs[1];
    for (int32_t offset = 0; offset < width*height; ++offset) {
        for (enum step step=0; step<QSTEPS; ++step)
        {
            const int x = offset % width;
            const int y = offset / width;
            const int next_x = x + delta_x[step];
            const int next_y = y + delta_y[step];

            const int ok = is_valid_move(width, height, goal_width, x, y, next_x, next_y);
            if (ok) {
                *ptr++ = offset + steps[step];
                continue;
            }

            int value = goal_status(width, height, goal_width, x, y, next_x, next_y);
            *ptr++ = value;
        }
    }

    me->qpoints = qpoints;
    me->connections = ptrs[1];
    return me;
}

void destroy_geometry(struct geometry * restrict const me)
{
    free(me);
}



void init_lines(
    const struct geometry * const geometry,
    uint8_t * restrict const lines)
{
    const int32_t * const connections = geometry->connections;
    const uint32_t qpoints = geometry->qpoints;

    for (int32_t point = 0; point < qpoints; ++point) {
        uint8_t mask = 0;
        for (enum step step=0; step<QSTEPS; ++step) {
            int32_t next = connections[QSTEPS*point+step];
            if (next == NO_WAY) {
                mask |= 1 << step;
            }
        }
        lines[point] = mask;
    }
}

struct state * create_state(const struct geometry * const geometry)
{
    const uint32_t qpoints = geometry->qpoints;
    const size_t sizes[2] = { sizeof(struct state), qpoints };
    void * ptrs[2];
    void * data = multialloc(2, sizes, ptrs, 64);

    if (data == NULL) {
        return NULL;
    }

    struct state * restrict const me = data;
    me->geometry = geometry;
    me->active = 1;
    me->ball = qpoints / 2;
    me->lines = ptrs[1];

    init_lines(geometry, me->lines);

    return me;
}

void destroy_state(struct state * restrict const me)
{
    free(me);
}

int state_copy(
    struct state * restrict const dest,
    const struct state * const src)
{
    if (src == dest) {
        return 0;
    }

    if (dest->geometry != src->geometry) {
        return EINVAL;
    }

    memcpy(dest->lines, src->lines, src->geometry->qpoints);
    dest->active = src->active;
    dest->ball = src->ball;
    return 0;
}

enum state_status state_status(const struct state * const me)
{
    const int ball = me->ball;

    if (ball == GOAL_1) {
        return WIN_1;
    }

    if (ball == GOAL_2) {
        return WIN_2;
    }

    const uint8_t * const lines = me->lines;
    const int is_dead_end = ball >= 0 && lines[ball] == 0xFF;
    if (is_dead_end) {
        return me->active == 1 ? WIN_2 : WIN_1;
    }

    return IN_PROGRESS;
}

steps_t state_get_steps(const struct state * const me)
{
    if (me->ball < 0) {
        return 0;
    }

    return me->lines[me->ball] ^ 0xFF;
}

int state_step(struct state * restrict const me, const enum step step)
{
    if (me->ball < 0) {
        return NO_WAY;
    }

    const int ball = me->ball;
    const uint8_t mask = 1 << step;
    if (me->lines[ball] & mask) {
        return NO_WAY;
    }

    const int32_t * const connections = me->geometry->connections;
    const int next = connections[QSTEPS*ball + step];
    me->ball = next;
    if (next < 0) {
        return next;
    }

    const int switch_active = me->lines[next] == 0;
    me->lines[ball] |= mask;
    me->lines[next] |= 1 << BACK(step);

    if (switch_active) {
        me->active ^= 3;
    }

    return next;
}



static int set_capacity(
    struct history * restrict const me,
    const unsigned int capacity)
{
    void * new_steps = realloc(me->steps, capacity * sizeof(enum step));
    if (new_steps == NULL) {
        return ENOMEM;
    }

    me->capacity = capacity;
    me->steps = new_steps;
    return 0;
}

void init_history(struct history * restrict const me)
{
    me->qsteps = 0;
    me->capacity = 0;
    me->steps = NULL;
}

void free_history(struct history * restrict const me)
{
    if (me->steps != NULL) {
        free(me->steps);
    }
}

int history_push(struct history * restrict const me, const enum step step)
{
    if (me->qsteps == me->capacity) {
        const int status = set_capacity(me, 2 * me->capacity + 128);
        if (status != 0) {
            return status;
        }
    }

    me->steps[me->qsteps++] = step;
    return 0;
}



#ifdef MAKE_CHECK

#include "insider.h"

#define BW    9
#define BH   11
#define GW    2

#define STOP  QSTEPS

static int make_point(const int x, const int y)
{
    return y * BW + x;
}

static void check_steps(
    const struct geometry * const me,
    const int x, const int y,
    const int * const expected)
{
    const int point = make_point(x, y);
    for (enum step step=0; step<QSTEPS; ++step) {
        const int next = me->connections[QSTEPS*point + step];
        if (next != expected[step]) {
            test_fail("Unexpected step: x=%d, y=%d, step=%d, next=%d, expected next=%d.", x, y, step, next, expected[step]);
        }
    }
}

static int apply_path(
    const struct geometry * const me,
    int point, const enum step path[])
{
    const enum step * current = path;
    while (point >= 0 && *current != STOP) {
        point = me->connections[QSTEPS*point + (*current++)];
    }

    return point;
}

static void check_map(
    const struct geometry * restrict const me,
    const int start, const int expected,
    const enum step path[])
{
    const int finish = apply_path(me, start, path);
    if (finish != expected) {
        test_fail("Unexpected apply_path: start=%d, finish=%d, expected finish=%d.", start, finish, expected);
    }
}

int test_std_geometry(void)
{
    struct geometry * restrict const me = create_std_geometry(BW, BH, GW);
    if (me == NULL) {
        test_fail("create_std_geometry(%d, %d, %d) fails, return value is NULL, errno is %d.",
            BW, BH, GW, errno);
    }

    const int center = make_point(BW/2, BH/2);

    const int expected_from_center[QSTEPS] = {
        make_point(3, 6), make_point(4, 6), make_point(5, 6), make_point(5, 5),
        make_point(5, 4), make_point(4, 4), make_point(3, 4), make_point(3, 5)
    };
    check_steps(me, 4, 5, expected_from_center);

    const int nw_corner[QSTEPS] = {
        NO_WAY, NO_WAY, NO_WAY, NO_WAY, make_point(1, 9), NO_WAY, NO_WAY, NO_WAY
    };
    check_steps(me, 0, 10, nw_corner);

    const int right_side[QSTEPS] = {
        make_point(7, 7), NO_WAY, NO_WAY, NO_WAY, NO_WAY, NO_WAY,
        make_point(7, 5), make_point(7, 6)
    };
    check_steps(me, 8, 6, right_side);

    const int bottom_side[QSTEPS] = {
        make_point(0, 1), make_point(1, 1), make_point(2, 1),
        NO_WAY, NO_WAY, NO_WAY, NO_WAY, NO_WAY
    };
    check_steps(me, 1, 0, bottom_side);

    const int goal_post[QSTEPS] = {
        GOAL_1, NO_WAY, NO_WAY, NO_WAY, make_point(6, 9),
        make_point(5, 9), make_point(4, 9), make_point(4, 10)
    };
    check_steps(me, 5, 10, goal_post);

    const int goal_line[QSTEPS] = {
        make_point(3, 1), make_point(4, 1), make_point(5, 1),
        make_point(5, 0), GOAL_2, GOAL_2, GOAL_2, make_point(3, 0)
    };
    check_steps(me, 4, 0, goal_line);

    static enum step cycle[] = {
        SOUTH_WEST, WEST, NORTH_WEST, SOUTH, EAST, NORTH, NORTH_EAST, SOUTH_EAST, STOP
    };
    check_map(me, center, center, cycle);

    static enum step out[] = { SOUTH_WEST, SOUTH_WEST, SOUTH_WEST, SOUTH_WEST, SOUTH_WEST };
    check_map(me, center, NO_WAY, out);

    static enum step goal1[]= { NORTH, NORTH, NORTH, NORTH, NORTH, NORTH_EAST };
    check_map(me, center,  GOAL_1, goal1);

    static enum step goal2[]= { SOUTH, SOUTH, SOUTH, SOUTH, SOUTH_WEST, SOUTH_EAST };
    check_map(me, center,  GOAL_2, goal2);

    destroy_geometry(me);
    return 0;
}

int test_step(void)
{
    struct geometry * restrict const geometry = create_std_geometry(BW, BH, GW);
    if (geometry == NULL) {
        test_fail("create_std_geometry(%d, %d, %d) failed, errno = %d.", BW, BH, GW, errno);
    }

    struct state * restrict const state = create_state(geometry);
    if (state == NULL) {
        test_fail("create_state(geometry) failed, errno = %d.", errno);
    }

    struct test_step {
        enum step step;
        int no_way_check;
        int is_done;
        int x, y;
        int status;
    };

    struct test_step test_steps[] = {
        { NORTH_EAST, 0, 1, 5, 6 }, { SOUTH_WEST, 1 },
        {      SOUTH, 0, 1, 5, 5 }, {      NORTH, 1 },
        { NORTH_EAST, 0, 1, 6, 6 }, { SOUTH_WEST, 1 },
        { SOUTH_EAST, 0, 1, 7, 5 }, { NORTH_WEST, 1 },
        { NORTH_EAST, 0, 0, 8, 6 }, { SOUTH_WEST, 1 }, { SOUTH, 1},
        { NORTH_WEST, 0, 1, 7, 7 }, { SOUTH_EAST, 1 },
        { SOUTH_WEST, 0, 0, 6, 6 }, { NORTH_EAST, 1 }, { SOUTH_EAST, 1 }, { SOUTH_WEST, 1 },
        {       WEST, 0, 0, 5, 6 }, { SOUTH_WEST, 1 }, { SOUTH, 1}, { EAST, 1},
        { SOUTH_EAST, 0, 1, 6, 5 }, { NORTH_WEST, 1 },
        {      NORTH, 0, 0, 6, 6 }, { SOUTH_EAST, 1 }, { SOUTH_WEST, 1 }, { WEST, 1}, { NORTH_EAST, 1}, { SOUTH, 1},
        {      NORTH, 0, 1, 6, 7 }, { SOUTH, 1},
        { SOUTH_EAST, 0, 1, 7, 6 }, { NORTH_WEST, 1 },
        {      NORTH, 0, 0, 7, 7 }, { SOUTH_EAST, 1 }, { SOUTH_WEST, 1 }, { SOUTH, 1},
        { NORTH_EAST, 0, 0, 8, 8 }, { SOUTH_WEST, 1 }, { SOUTH, 1},
        { NORTH_WEST, 0, 1, 7, 9 }, { SOUTH_EAST, 1 },
        {       WEST, 0, 1, 6, 9 }, { EAST, 1},
        { NORTH_WEST, 0, 0, 5,10 }, { SOUTH_EAST, 1 }, { EAST, 1},
        {       WEST, 0, 1, 4,10 }, { EAST, 1},
        { NORTH_WEST, 1, 0, 0, 0, +1 },
        { QSTEPS }
    };

    const struct test_step * test_step = test_steps;
    for (; test_step->step != QSTEPS; ++test_step)
    {
        const int prev_active = state->active;
        const int prev_ball = state->ball;
        const int index = test_step - test_steps;

        const int next = state_step(state, test_step->step);
        if (test_step->no_way_check) {
            if (test_step->status == 0) {
                if (next != NO_WAY) {
                    test_fail("state_step on move %d: NO_WAY expected, but next = %d.", index, next);
                }
                if (state->active != prev_active) {
                    test_fail("state_step on move %d: active corrupted in NO_WAY test, active=%d, expected=%d.", index, state->active, prev_active);
                }
                if (state->ball != prev_ball) {
                    test_fail("state_step on move %d: ball corrupted in NO_WAY test, active=%d, expected=%d.", index, state->ball, prev_ball);
                }
            } else {
                if (test_step->status > 0 && next != GOAL_1) {
                    test_fail("state_step on move %d: next is %d, but GOAL_1 (%d) expected..", index, next, GOAL_1);
                }
                if (test_step->status < 0 && next != GOAL_2) {
                    test_fail("state_step on move %d: next is %d, but GOAL_2 (%d) expected..", index, next, GOAL_2);
                }
                if (next != state->ball) {
                    test_fail("state_step on move %d: %d is returned, but state->ball is %d.", index, next, state->ball);
                }
            }
        } else {
            const int expected = make_point(test_step->x, test_step->y);
            if (next != expected) {
                test_fail("state_step on move %d: %d is returned, but %d expected.", index, next, expected);
            }
            if (next != state->ball) {
                test_fail("state_step on move %d: %d is returned, but state->ball is %d.", index, next, state->ball);
            }
            if (test_step->is_done == (state->active == prev_active)) {
                test_fail("state_step on move %d: is_done=%d, but old %d, new %d.", index, test_step->is_done, prev_active, state->active);
            }
        }
    }

    destroy_state(state);
    destroy_geometry(geometry);
    return 0;
}

#endif
