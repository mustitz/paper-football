#include "paper-football.h"

#include <stdio.h>

#define ERROR_BUF_SZ   256

struct random_ai
{
    struct state * state;
    struct state * backup;
    char * error_buf;
};

static const struct ai_param terminator = { NULL, NULL, NO_TYPE, 0 };

struct random_ai * create_random_ai(const struct geometry * const geometry)
{
    const uint32_t qpoints = geometry->qpoints;
    const size_t sizes[6] = {
        sizeof(struct random_ai),
        sizeof(struct state),
        qpoints,
        sizeof(struct state),
        qpoints,
        ERROR_BUF_SZ
    };

    void * ptrs[6];
    void * data = multialloc(6, sizes, ptrs, 64);

    if (data == NULL) {
        return NULL;
    }

    struct random_ai * restrict const me = data;
    struct state * restrict const state = ptrs[1];
    uint8_t * restrict const lines = ptrs[2];
    struct state * restrict const backup = ptrs[3];
    uint8_t * restrict const backup_lines = ptrs[4];
    char * const error_buf = ptrs[5];

    me->state = state;
    me->backup = backup;
    me->error_buf = error_buf;

    state->geometry = geometry;
    state->lines = lines;
    state->active = 1;
    state->ball = qpoints / 2;

    backup->geometry = geometry;
    backup->lines = backup_lines;

    init_lines(geometry, lines);
    return me;
}

void free_random_ai(struct ai * restrict const ai)
{
    free(ai->data);
}

int random_ai_reset(
    struct ai * restrict const ai,
    const struct geometry * const geometry)
{
    ai->error = NULL;

    struct random_ai * restrict const me = create_random_ai(geometry);
    if (me == NULL) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "Bad alloc for create_random_ai.");
        ai->error = me->error_buf;
        return errno;
    }

    free_random_ai(ai);
    ai->data = me;
    return 0;
}

int random_ai_do_step(
    struct ai * restrict const ai,
    const enum step step)
{
    ai->error = NULL;
    struct random_ai * restrict const me = ai->data;
    const int next = state_step(me->state, step);

    if (next == NO_WAY) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "Direction occupied.");
        ai->error = me->error_buf;
        return EINVAL;
    }

    return 0;
}

static void restore_backup(struct random_ai * restrict const me)
{
    struct state * old_state = me->state;
    me->state = me->backup;
    me->backup = old_state;
}

int random_ai_do_steps(
    struct ai * restrict const ai,
    const unsigned int qsteps,
    const enum step steps[])
{
    ai->error = NULL;
    struct random_ai * restrict const me = ai->data;

    state_copy(me->backup, me->state);

    const enum step * ptr = steps;
    const enum step * const end = ptr + qsteps;
    for (; ptr != end; ++ptr) {
        const int next = state_step(me->state, *ptr);
        if (next == NO_WAY) {
            const int index = ptr - steps;
            snprintf(me->error_buf, ERROR_BUF_SZ, "Error on step %d: direction  occupied.", index);
            ai->error = me->error_buf;
            restore_backup(me);
            return EINVAL;
        }
    }

    return 0;
}

enum step random_ai_go(
    struct ai * restrict const ai,
    struct step_stat * restrict const stats)
{
    ai->error = NULL;
    struct random_ai * restrict const me = ai->data;

    steps_t steps = state_get_steps(me->state);
    if (steps == 0) {
        errno = EINVAL;
        return INVALID_STEP;
    }

    enum step alternatives[QSTEPS];
    int qalternatives = 0;
    for (enum step step=0; step<QSTEPS; ++step) {
        const steps_t mask = 1 << step;
        const int possible = (mask & steps) != 0;

        if (stats) {
            stats[step].possible = possible;
            stats[step].qgames = -1;
            stats[step].score = 0;
        }

        if (possible) {
            alternatives[qalternatives++] = step;
        }
    }

    const int choice =  qalternatives > 1 ? rand() % qalternatives : 0;
    return alternatives[choice];
}

const struct ai_param * random_ai_get_params(const struct ai * const ai)
{
    return &terminator;
}

int random_ai_set_param(
    struct ai * restrict const ai,
    const char * const name,
    const void * const value)
{
    return EINVAL;
}

const struct state * random_ai_get_state(const struct ai * const ai)
{
    struct random_ai * restrict const me = ai->data;
    return me->state;
}

int init_random_ai(
    struct ai * restrict const ai,
    const struct geometry * const geometry)
{
    ai->error = NULL;

    ai->data = create_random_ai(geometry);
    if (ai->data == NULL) {
        ai->error = "Bad alloc for create_random_ai.";
        return errno;
    }

    ai->reset = random_ai_reset;
    ai->do_step = random_ai_do_step;
    ai->do_steps = random_ai_do_steps;
    ai->go = random_ai_go;
    ai->get_params = random_ai_get_params;
    ai->set_param = random_ai_set_param;
    ai->get_state = random_ai_get_state;
    ai->free = free_random_ai;
    return 0;
}



#ifdef MAKE_CHECK

#include "insider.h"

#define BW    9
#define BH   11
#define GW    2

int test_random_ai(void)
{
    struct geometry * restrict const geometry = create_std_geometry(BW, BH, GW);
    if (geometry == NULL) {
        test_fail("create_std_geometry(%d, %d, %d) fails, return value is NULL, errno is %d.",
            BW, BH, GW, errno);
    }

    struct ai storage;
    struct ai * restrict const ai = &storage;

    init_random_ai(ai, geometry);

    int status;

    status = ai->do_step(ai, SOUTH_WEST);
    if (status != 0) {
        test_fail("do_step(SW) failed with status %d.", status);
    }

    enum step steps[4] = { WEST, SOUTH, SOUTH_WEST, SOUTH_WEST };
    status = ai->do_steps(ai, 4, steps);
    if (status != 0) {
        test_fail("do_steps(W S SW SW) failed with status %d.", status);
    }
    if (ai->error != NULL) {
        test_fail("do_steps(W S SW SW) is OK, but ai->error is set.");
    }

    const steps_t possible = state_get_steps(ai->get_state(ai));
    for (int i=0; i<100; ++i) {
        enum step step = ai->go(ai, NULL);
        steps_t mask = 1 << step;
        if ((mask & possible) == 0) {
            test_fail("go() tried to return impossible step %d.", step);
        }
    }

    enum step bad_steps[2] = { SOUTH_EAST, EAST };
    status = ai->do_steps(ai, 2, bad_steps);
    if (status == 0) {
        test_fail("do_steps(SE E) failture expected, but statis is 0.");
    }
    if (ai->error == NULL) {
        test_fail("do_steps(SE E) failture, but ai->error is not set.");
    }
    if (strlen(ai->error) >= ERROR_BUF_SZ) {
        test_fail("do_steps(SE E) failture, error message too large.");
    }

    enum step good_steps[4] = { SOUTH_EAST, NORTH_EAST, SOUTH_EAST, SOUTH_EAST };
    status = ai->do_steps(ai, 4, good_steps);
    if (status != 0) {
        test_fail("do_steps(SE NE SE SE) failed with status %d.", status);
    }
    if (ai->error != NULL) {
        test_fail("do_steps(SE NE SE SE) is OK, but ai->error is set.");
    }

    ai->free(ai);

    destroy_geometry(geometry);
    return 0;
}

#endif
