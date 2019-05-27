#include "paper-football.h"

size_t param_sizes[QPARAM_TYPES] = {
    [U32] = sizeof(uint32_t),
    [I32] = sizeof(int32_t),
    [F32] = sizeof(float),
};

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

struct geometry * create_std_geometry(
    const int width,
    const int height,
    const int goal_width,
    const int free_kick_len)
{
    const int status = check_std_arg(width, height, goal_width, free_kick_len);
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
    me->free_kick_len = free_kick_len;
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
}

struct state * create_state(const struct geometry * const geometry)
{
    return NULL;
}

void destroy_state(struct state * restrict const me)
{
}

int state_copy(
    struct state * restrict const dest,
    const struct state * const src)
{
    if (src == dest) {
        return 0;
    }

    return EINVAL;
}

enum state_status state_status(const struct state * const me)
{
    return IN_PROGRESS;
}

steps_t state_get_steps(const struct state * const me)
{
    return 0;
}

int state_step(struct state * restrict const me, const enum step step)
{
    return INVALID_STEP;
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
    return EINVAL;
}

#ifdef MAKE_CHECK

#include "insider.h"

int test_std_geometry(void)
{
    return 0;
}

int test_step(void)
{
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
