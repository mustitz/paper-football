#include "paper-football.h"

size_t param_sizes[QPARAM_TYPES] = {
    [U32] = sizeof(uint32_t),
    [I32] = sizeof(int32_t),
    [F32] = sizeof(float),
};

struct geometry * create_std_geometry(
    const int width,
    const int height,
    const int goal_width,
    const int free_kick_len)
{
    return NULL;
}

void destroy_geometry(struct geometry * restrict const me)
{
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
