#include "paper-football.h"

size_t param_sizes[QPARAM_TYPES] = {
    [U32] = sizeof(uint32_t),
    [I32] = sizeof(int32_t),
    [F32] = sizeof(float),
};

struct three_step {
    enum step step1;
    steps_t possible;
};

steps_t magic_step3[64];



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

static inline int check_std_arg(
    const int width,
    const int height,
    const int goal_width,
    const int free_kick_len)
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

    const int invalid_free_kick = 0
        || free_kick_len <= 3
        || free_kick_len >= width/2
        || free_kick_len >= height/2
    ;

    if (invalid_free_kick) {
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

static int init_magic_step3(void)
{
    if (magic_step3[0] == 0xEF) {
        return 0;
    }

    static int deltas[QSTEPS] = { +7, +8, +9, +1, -7, -8, -9, -1 };
    const int ball0 = 42;
    steps_t * restrict ptr = magic_step3;
    for (enum step step1=0; step1<QSTEPS; ++step1) {
        const int ball1 = ball0 + deltas[step1];
        for (enum step step2=0; step2<QSTEPS; ++step2) {
            const int ball2 = ball1 + deltas[step2];
            if (ball0 == ball2) {
                *ptr++ = 0;
                continue;
            }

            steps_t possible = 0;
            steps_t mask = 1;
            for (enum step step3=0; step3<QSTEPS; ++step3) {
                const int ball3 = ball2 + deltas[step3];
                if (ball3 != ball0 && ball3 != ball1) {
                    possible |= mask;
                }
                mask <<= 1;
            }

            *ptr++ = possible;
        }
    }

    if (ptr - magic_step3 != 64) {
        memset(magic_step3, 0, sizeof(magic_step3));
        return EINVAL;
    }

    return 0;
}

struct geometry * create_std_geometry(
    const int width,
    const int height,
    const int goal_width,
    const int free_kick_len)
{
    int status;
    status = init_magic_step3();
    if (status) {
        errno = status;
        return NULL;
    }

    status = check_std_arg(width, height, goal_width, free_kick_len);
    if (status) {
        return NULL;
    }

    const uint32_t qpoints = (uint32_t)(width) * (uint32_t)(height);
    const size_t board_map_sz = qpoints * QSTEPS * sizeof(uint32_t);
    const size_t sizes[3] = { sizeof(struct geometry), board_map_sz, board_map_sz };
    void * ptrs[3];
    void * data = multialloc(3, sizes, ptrs, 256);

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

    const int32_t * const connections = ptrs[1];
    ptr = ptrs[2];
    for (int32_t offset = 0; offset < width*height; ++offset) {
        for (enum step step=0; step<QSTEPS; ++step)
        {
            int ball = offset;
            for (int i=0; i<free_kick_len; ++i) {
                ball = connections[ball*QSTEPS + step];
                if (ball < 0) {
                    break;
                }
            }
            *ptr++ = ball;
        }
    }

    me->qpoints = qpoints;
    me->free_kick_len = free_kick_len;
    me->connections = ptrs[1];
    me->free_kicks = ptrs[2];
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

static inline void mark_occuped(
    struct state * restrict const me,
    const int point)
{
    const int32_t * const connections = me->geometry->connections;
    uint8_t * restrict const lines = me->lines;

    const int32_t * ptr = connections + QSTEPS*point;
    for (enum step step=0; step<QSTEPS; ++step) {
        const int32_t next = *ptr;
        if (next >= 0) {
            enum step back = BACK(step);
            uint8_t mask = 1 << back;
            lines[next] |= mask;
        }
        ++ptr;
    }
}

static inline void mark_diag(
    struct state * restrict const me,
    const int point,
    const enum step step)
{

    if ((step & 1) == 1) {
        /* This is not a diagonal step */
        return;
    }

    const int32_t * const connections = me->geometry->connections + QSTEPS * point;
    uint8_t * restrict const lines = me->lines;

    enum step prev_step = (step - 1) & 0x07;
    enum step next_step = (step + 1) & 0x07;

    const int prev_point = connections[prev_step];
    const int next_point = connections[next_step];

    enum step prev_ortogonal = (step + 2) & 0x07;
    enum step next_ortogonal = (step - 2) & 0x07;

    lines[prev_point] |= (1 << prev_ortogonal);
    lines[next_point] |= (1 << next_ortogonal);
}

static uint64_t state_gen_step12(const struct state * const me)
{
    uint64_t result = 0;
    const int ball0 = me->ball;
    const int32_t * const connections = me->geometry->connections;
    const uint8_t * const lines = me->lines;

    steps_t possible0 = lines[ball0] ^ 0xFF;
    while (possible0 != 0) {
        enum step step1 = extract_step(&possible0);
        const int ball1 = connections[QSTEPS*ball0 + step1];
        if (ball1 < 0) {
            result |= 0xFF << (8*step1);
            continue;
        }

        steps_t possible1 = ball1 < 0 ? 0xFF : lines[ball1] ^ 0xFF;
        while (possible1 != 0) {
            enum step step2 = extract_step(&possible1);
            const int ball2 = connections[QSTEPS*ball1 + step2];
            const int index = step1*8 + step2;
            if (ball2 < 0) {
                result |= 1ull << index;
                continue;
            }
            steps_t ball_lines = ball2 >= 0 ? lines[ball2] : 0;
            steps_t possible2 = (ball_lines ^ 0xFF) & magic_step3[index];
            if (possible2 != 0) {
                result |= 1ull << index;
            }
        }
    }

    return result;
}

static steps_t get_first_steps(const struct state * const me)
{
    const uint64_t step12 = me->step12;
    const int step_NW = ((step12 >>  0) & 0xFF) != 0;
    const int step_N  = ((step12 >>  8) & 0xFF) != 0;
    const int step_NE = ((step12 >> 16) & 0xFF) != 0;
    const int step_E  = ((step12 >> 24) & 0xFF) != 0;
    const int step_SE = ((step12 >> 32) & 0xFF) != 0;
    const int step_S  = ((step12 >> 40) & 0xFF) != 0;
    const int step_SW = ((step12 >> 48) & 0xFF) != 0;
    const int step_W  = ((step12 >> 56) & 0xFF) != 0;
    return 0
        | (step_NW << 0)
        | (step_N  << 1)
        | (step_NE << 2)
        | (step_E  << 3)
        | (step_SE << 4)
        | (step_S  << 5)
        | (step_SW << 6)
        | (step_W  << 7)
        ;
}

static inline steps_t get_free_kicks(const struct state * const me)
{
    const int32_t * ptr = me->geometry->free_kicks + QSTEPS * me->ball;
    const int32_t * const end = ptr + QSTEPS;
    steps_t mask = 1;
    steps_t result = 0;
    for (; ptr != end; ++ptr) {
        if (*ptr != NO_WAY) {
            result |= mask;
        }
        mask <<= 1;
    }
    return result;
}

static steps_t get_second_steps(const struct state * const me)
{
    return 0xFF & (me->step12 >> (me->step1 << 3));
}

struct state * create_state(const struct geometry * const geometry)
{
    const uint32_t qpoints = geometry->qpoints;
    const int ball = qpoints / 2;

    const size_t sizes[2] = { sizeof(struct state), qpoints };
    void * ptrs[2];
    void * data = multialloc(2, sizes, ptrs, 64);

    if (data == NULL) {
        return NULL;
    }

    struct state * restrict const me = data;
    me->geometry = geometry;
    me->active = 1;
    me->ball = ball;
    me->ball_before_goal = NO_WAY;
    me->lines = ptrs[1];

    init_lines(geometry, me->lines);

    me->step1 = INVALID_STEP;
    me->step2 = INVALID_STEP;
    mark_occuped(me, ball);
    me->step12 = state_gen_step12(me);

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
    dest->ball_before_goal = src->ball_before_goal;
    dest->step1 = src->step1;
    dest->step2 = src->step2;
    dest->step12 = src->step12;
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

    return IN_PROGRESS;
}


steps_t state_get_steps(const struct state * const me)
{
    if (me->step1 == INVALID_STEP) {
        return me->step12 ? get_first_steps(me) : get_free_kicks(me);
    }

    if (me->step2 == INVALID_STEP) {
        return get_second_steps(me);
    }

    return 0xFF ^ me->lines[me->ball];
}

static inline int last_step(struct state * restrict const me, const int next)
{
    me->ball = next;
    me->step12 = state_gen_step12(me);
    if (me->step12 != 0) {
        me->active ^= 3;
    }
    return next;
}

static inline int free_kick_step(struct state * restrict const me, const enum step step)
{
    int ball = me->ball;
    const int32_t * const free_kicks = me->geometry->free_kicks + QSTEPS * ball;
    const int32_t * const connections = me->geometry->connections;
    const int32_t destination = free_kicks[step];
    if (destination == NO_WAY) {
        return NO_WAY;
    }

    const int free_kick_len = me->geometry->free_kick_len;
    int next = ball;
    for (int i=0; i<free_kick_len; ++i) {
        next = connections[QSTEPS * next + step];
        if (next < 0) {
            break;
        }
        mark_occuped(me, next);
        mark_diag(me, next, step);
    }

    return last_step(me, next);
}

int state_step(struct state * restrict const me, const enum step step)
{
    const int32_t * const connections = me->geometry->connections;
    const int ball = me->ball;
    if (ball < 0) {
        return ball;
    }

    const int32_t next = connections[QSTEPS*ball + step];
    if (next < 0) {
        if (next != NO_WAY) {
            me->ball = next;
        }
        return next;
    }

    const steps_t mask = 1 << step;

    if (me->step1 == INVALID_STEP) {
        if (me->step12 == 0) {
            return free_kick_step(me, step);
        }
        steps_t steps = get_first_steps(me);
        const int occupied = (steps & mask) == 0;
        if (occupied) {
            return NO_WAY;
        }
        mark_occuped(me, next);
        mark_diag(me, ball, step);
        me->step1 = step;
        return me->ball = next;
    }

    if (me->step2 == INVALID_STEP) {
        steps_t steps = get_second_steps(me);
        const int occupied = (steps & mask) == 0;
        if (occupied) {
            return NO_WAY;
        }
        mark_occuped(me, next);
        mark_diag(me, ball, step);
        me->step2 = step;
        return me->ball = next;
    }

    steps_t steps = 0xFF ^ me->lines[ball];
    const int occupied = (steps & mask) == 0;
    if (occupied) {
        return NO_WAY;
    }
    mark_occuped(me, next);
    mark_diag(me, ball, step);
    me->step1 = INVALID_STEP;
    me->step2 = INVALID_STEP;
    return last_step(me, next);
}

int state_unstep(struct state * restrict const me, const enum step step)
{
    return INVALID_STEP;
}



void init_history(struct history * restrict const me)
{
}

void free_history(struct history * restrict const me)
{
}

int history_push(struct history * restrict const me, const enum step step)
{
    return 0;
}

#ifdef MAKE_CHECK

#include "insider.h"

#define BW   15
#define BH   23
#define GW    4
#define FK    5

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

static void check_free_kick(
    const struct geometry * const me,
    const int x, const int y,
    const int * const expected)
{
    const int point = make_point(x, y);
    for (enum step step=0; step<QSTEPS; ++step) {
        const int next = me->free_kicks[QSTEPS*point + step];
        if (next != expected[step]) {
            test_fail("Unexpected free kick: x=%d, y=%d, step=%d, next=%d, expected next=%d.", x, y, step, next, expected[step]);
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
    struct geometry * restrict const me = create_std_geometry(BW, BH, GW, FK);
    if (me == NULL) {
        test_fail("create_std_geometry(%d, %d, %d, %d) fails, return value is NULL, errno is %d.",
            BW, BH, GW, FK, errno);
    }

    if (me->free_kick_len != FK) {
        test_fail("free_kick_len is set wrong, actual value is %d, expected %d.", me->free_kick_len, FK);
    }

    const int center = make_point(BW/2, BH/2);

    const int expected_from_center[QSTEPS] = {
        make_point(6, 12), make_point(7, 12), make_point(8, 12), make_point(8, 11),
        make_point(8, 10), make_point(7, 10), make_point(6, 10), make_point(6, 11)
    };
    check_steps(me, 7, 11, expected_from_center);

    const int nw_corner[QSTEPS] = {
        NO_WAY, NO_WAY, NO_WAY, NO_WAY, make_point(1, BH-2), NO_WAY, NO_WAY, NO_WAY
    };
    check_steps(me, 0, BH-1, nw_corner);

    const int right_side[QSTEPS] = {
        make_point(BW-2, 7), NO_WAY, NO_WAY, NO_WAY, NO_WAY, NO_WAY,
        make_point(BW-2, 5), make_point(BW-2, 6)
    };
    check_steps(me, BW-1, 6, right_side);

    const int bottom_side[QSTEPS] = {
        make_point(0, 1), make_point(1, 1), make_point(2, 1),
        NO_WAY, NO_WAY, NO_WAY, NO_WAY, NO_WAY
    };
    check_steps(me, 1, 0, bottom_side);

    const int post_x = BW/2 + GW/2;
    const int goal_post[QSTEPS] = {
        GOAL_1, NO_WAY, NO_WAY, NO_WAY, make_point(post_x+1, BH-2),
        make_point(post_x, BH-2), make_point(post_x-1, BH-2), make_point(post_x-1, BH-1)
    };
    check_steps(me, post_x, BH-1, goal_post);

    const int goal_line[QSTEPS] = {
        make_point(BW/2-1, 1), make_point(BW/2, 1), make_point(BW/2+1, 1),
        make_point(BW/2+1, 0), GOAL_2, GOAL_2, GOAL_2, make_point(BW/2-1, 0)
    };
    check_steps(me, BW/2, 0, goal_line);

    static enum step cycle[] = {
        SOUTH_WEST, WEST, NORTH_WEST, SOUTH, EAST, NORTH, NORTH_EAST, SOUTH_EAST, STOP
    };
    check_map(me, center, center, cycle);

    static enum step out[BW/2+1] = { [ 0 ... BW/2] = SOUTH_WEST };
    check_map(me, center, NO_WAY, out);

    static enum step goal1[BH/2+1]= { [ 0 ... BH/2-1] = NORTH, [BH/2] = NORTH_EAST };
    check_map(me, center,  GOAL_1, goal1);

    static enum step goal2[BH/2+1]= { [ 0 ... BH/2-2] = SOUTH, [BH/2-1] = SOUTH_WEST, [BH/2] = SOUTH_EAST };
    check_map(me, center,  GOAL_2, goal2);

    const int expected_1[QSTEPS] = {
        NO_WAY, NO_WAY, GOAL_1, make_point(7, 19),
        make_point(7, 14), make_point(2, 14), NO_WAY, NO_WAY
    };
    check_free_kick(me, 2, 19, expected_1);

    const int expected_2[QSTEPS] = {
        make_point( 4, 22), make_point( 9, 22), make_point(14, 22), make_point(14, 17),
        make_point(14, 12), make_point( 9, 12), make_point( 4, 12), make_point( 4, 17)
    };
    check_free_kick(me, 9, 17, expected_2);

    destroy_geometry(me);
    return 0;
}

int test_magic_step3(void)
{
    const int status = init_magic_step3();
    if (status != 0) {
        test_fail("init_all_three_steps failed with code %d.", status);
    }

    const int status2 = init_magic_step3();
    if (status2 != 0) {
        test_fail("init_all_three_steps (second call) failed with code %d.", status2);
    }

    int qbits = 0;
    const steps_t * ptr = magic_step3;
    const steps_t * const end = ptr + 64;
    for (; ptr != end; ++ptr) {
        qbits += step_count(*ptr);
    }

    if (qbits != 368) {
        test_fail("Invalid qbist, actual value is %d, expected %d.", qbits, 368);
    }

    return 0;
}

int test_step(void)
{
    struct geometry * restrict const geometry = create_std_geometry(BW, BH, GW, FK);
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
        {      NORTH, 0, 0,  7, 12 }, { SOUTH, 1},
        { SOUTH_WEST, 0, 0,  6, 11 }, { NORTH_EAST, 1}, { EAST, 1},
        {      NORTH, 0, 1,  6, 12 }, { EAST, 1}, { SOUTH_EAST, 1}, { SOUTH, 1},
        { SOUTH_WEST, 0, 0,  5, 11 }, { NORTH_EAST, 1}, { EAST, 1},
        { SOUTH_EAST, 0, 0,  6, 10 }, { NORTH_WEST, 1}, { NORTH, 1 }, { NORTH_EAST, 1},
        {      SOUTH, 0, 1,  6,  9 }, { NORTH, 1 },
        {       WEST, 0, 0,  5,  9 }, { NORTH_EAST, 1}, { EAST, 1},
        { NORTH_WEST, 0, 0,  4, 10 }, { NORTH_EAST, 1}, { SOUTH_EAST, 1},
        {       EAST, 0, 1,  5, 10 }, { NORTH, 1 }, { NORTH_EAST, 1}, { EAST, 1}, { SOUTH_EAST, 1}, { SOUTH, 1}, { SOUTH_WEST, 1}, { WEST, 1},
        { NORTH_WEST, 0, 0,  4, 11 }, { EAST, 1}, { SOUTH_EAST, 1}, { SOUTH, 1},
        { SOUTH_WEST, 0, 0,  3, 10 }, { NORTH_EAST, 1}, { EAST, 1},
        { SOUTH_EAST, 0, 1,  4,  9 }, { NORTH_WEST, 1}, { NORTH, 1 }, { NORTH_EAST, 1}, { EAST, 1},
        {       WEST, 0, 0,  3,  9 }, { NORTH, 1 }, { NORTH_EAST, 1}, { EAST, 1},
        { NORTH_WEST, 0, 0,  2, 10 }, { EAST, 1}, { SOUTH_EAST, 1},
        { NORTH_EAST, 0, 1,  3, 11 }, { EAST, 1}, { SOUTH_EAST, 1}, { SOUTH, 1}, { SOUTH_WEST, 1},
        { NORTH_WEST, 0, 0,  2, 12 }, { SOUTH_EAST, 1},
        { NORTH_WEST, 0, 0,  1, 13 }, { SOUTH_EAST, 1},
        { NORTH_WEST, 0, 1,  0, 14 }, { NORTH_WEST, 1}, { NORTH, 1 }, { SOUTH_EAST, 1}, { SOUTH, 1}, { SOUTH_WEST, 1}, { WEST, 1},
        {       EAST, 0, 0,  1, 14 }, { SOUTH, 1}, { SOUTH_WEST, 1}, { WEST, 1},
        { SOUTH_EAST, 0, 0,  2, 13 }, { SOUTH, 1}, { SOUTH_WEST, 1}, { WEST, 1},
        {       EAST, 0, 1,  3, 13 }, { SOUTH_WEST, 1}, { WEST, 1},
        {       EAST, 0, 0,  4, 13 }, { WEST, 1},
        { SOUTH_EAST, 0, 0,  5, 12 }, { NORTH_WEST, 1}, { EAST, 1}, { SOUTH_EAST, 1}, { SOUTH, 1}, { SOUTH_WEST, 1},
        {       WEST, 0, 0,  4, 12 }, { NORTH_WEST, 1}, { SOUTH_WEST, 1}, { WEST, 1},
        { NORTH_EAST, 0, 1,  9, 17 }, { SOUTH_WEST, 1},
        {       WEST, 0, 0,  8, 17 }, { EAST, 1}, { SOUTH_EAST, 1}, { SOUTH, 1},
        {      NORTH, 0, 0,  8, 18 }, { SOUTH_EAST, 1}, { SOUTH, 1},
        {      NORTH, 0, 1,  8, 19 }, { SOUTH, 1},
        {      NORTH, 0, 0,  8, 20 }, { SOUTH, 1},
        {      NORTH, 0, 0,  8, 21 }, { SOUTH, 1},
        {      NORTH, 0, 1,  8, 22 }, { SOUTH, 1},
        {       WEST, 0, 0,  7, 22 }, { EAST, 1}, { SOUTH_EAST, 1},
        { NORTH_EAST, 1, 0, 0, 0, +1},
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

int test_history(void)
{
    return 0;
}

int test_unstep(void)
{
    return 0;
}

#endif
