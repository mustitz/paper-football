#include "paper-football.h"

#include <stdio.h>

#define ERROR_BUF_SZ   256

struct random_ai
{
    struct state * state;
    struct state * backup;
    char * error_buf;
};

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
    ai->free = free_random_ai;
    return 0;
}