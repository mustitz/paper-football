#include "paper-football.h"

#include <stdio.h>
#include <time.h>

#define ERROR_BUF_SZ   256

struct random_ai
{
    struct state * state;
    struct state * backup;
    char * error_buf;
    struct step_stat * stats;
};

static const struct ai_param terminator = { NULL, NULL, NO_TYPE, 0 };

struct random_ai * create_random_ai(const struct geometry * const geometry)
{
    const uint32_t qpoints = geometry->qpoints;
    const size_t sizes[7] = {
        sizeof(struct random_ai),
        sizeof(struct state),
        qpoints,
        sizeof(struct state),
        qpoints,
        ERROR_BUF_SZ,
        QSTEPS * sizeof(struct step_stat)
    };

    void * ptrs[7];
    void * data = multialloc(7, sizes, ptrs, 64);

    if (data == NULL) {
        return NULL;
    }

    struct random_ai * restrict const me = data;
    struct state * restrict const state = ptrs[1];
    uint8_t * restrict const lines = ptrs[2];
    struct state * restrict const backup = ptrs[3];
    uint8_t * restrict const backup_lines = ptrs[4];
    char * const error_buf = ptrs[5];
    struct step_stat * stats = ptrs[6];

    me->state = state;
    me->backup = backup;
    me->error_buf = error_buf;
    me->stats = stats;

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
    free_history(&ai->history);
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

    struct history * restrict const history = &ai->history;
    const int status = history_push(history, step);
    if (status != 0) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "Bad history push, return code is %d.", status);
        return status;
    }

    const int next = state_step(me->state, step);
    if (next == NO_WAY) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "Direction occupied.");
        ai->error = me->error_buf;
        --history->qsteps;
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

    struct history * restrict const history = &ai->history;
    const unsigned int old_qsteps = history->qsteps;
    for (unsigned int i=0; i<qsteps; ++i) {
        const int status = history_push(history, steps[i]);
        if (status != 0) {
            snprintf(me->error_buf, ERROR_BUF_SZ, "Bad history push, return code is %d.", status);
            history->qsteps = old_qsteps;
            return status;
        }
    }

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
            history->qsteps = old_qsteps;
            return EINVAL;
        }
    }

    return 0;
}

int random_ai_undo_step(
    struct ai * restrict const ai)
{
    ai->error = NULL;
    struct random_ai * restrict const me = ai->data;

    struct history * restrict const history = &ai->history;
    if (history->qsteps == 0) {
        return EINVAL;
    }

    const enum step step = history->steps[history->qsteps-1];
    const int next = state_unstep(me->state, step);
    if (next < 0) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "Impossible unstep.");
        ai->error = me->error_buf;
        return EINVAL;
    }

    --history->qsteps;
    return 0;
}

int random_ai_undo_steps(
    struct ai * restrict const ai,
    const unsigned int qsteps)
{
    ai->error = NULL;
    struct random_ai * restrict const me = ai->data;

    struct history * restrict const history = &ai->history;
    if (history->qsteps < qsteps) {
        return EINVAL;
    }

    state_copy(me->backup, me->state);

    for (unsigned int i=0; i<qsteps; ++i) {
        const unsigned int index = history->qsteps - i - 1;
        const enum step step = history->steps[index];
        const int ball = state_unstep(me->state, step);
        if (ball < 0) {
            snprintf(me->error_buf, ERROR_BUF_SZ, "Error on unstep %d: impossible.", i);
            ai->error = me->error_buf;
            restore_backup(me);
            return EINVAL;
        }
    }

    history->qsteps -= qsteps;
    return 0;
}

enum step random_ai_go(
    struct ai * restrict const ai,
    struct ai_explanation * restrict const explanation)
{
    double start = clock();

    ai->error = NULL;
    struct random_ai * restrict const me = ai->data;

    steps_t steps = state_get_steps(me->state);
    if (steps == 0) {
        errno = EINVAL;
        return INVALID_STEP;
    }

    struct step_stat * restrict stats = me->stats;
    if (explanation) {
    }

    enum step alternatives[QSTEPS];
    int qalternatives = 0;
    for (enum step step=0; step<QSTEPS; ++step) {
        const steps_t mask = 1 << step;
        const int possible = (mask & steps) != 0;

        if (possible) {
            alternatives[qalternatives++] = step;

            if (explanation) {
                stats->step = step;
                stats->qgames = -1;
                stats->score = 0.5;
                ++stats;
            }
        }
    }

    const int choice =  qalternatives > 1 ? rand() % qalternatives : 0;
    enum step result = alternatives[choice];

    if (explanation) {
        double finish = clock();
        explanation->time = (finish - start) / CLOCKS_PER_SEC;
        explanation->score = 0.5;
        const size_t qstats = stats - me->stats;
        explanation->qstats = qstats > 1 ? qstats : 0;
        explanation->stats = qstats > 1 ? me->stats : NULL;
    }

    return result;
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

    init_history(&ai->history);

    ai->reset = random_ai_reset;
    ai->do_step = random_ai_do_step;
    ai->do_steps = random_ai_do_steps;
    ai->undo_step = random_ai_undo_step;
    ai->undo_steps = random_ai_undo_steps;
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

int test_random_ai_unstep(void)
{
    struct geometry * restrict const geometry = create_std_geometry(BW, BH, GW);
    if (geometry == NULL) {
        test_fail("create_std_geometry(%d, %d, %d) fails, return value is NULL, errno is %d.",
            BW, BH, GW, errno);
    }

    struct ai storage;
    struct ai * restrict const ai = &storage;

    init_random_ai(ai, geometry);

    const struct state * const state = ai->get_state(ai);
    while (state_status(state) == IN_PROGRESS) {
        const enum step step = ai->go(ai, NULL);
        ai->do_step(ai, step);
    }

    const int status = ai->undo_steps(ai, ai->history.qsteps);
    if (status != 0) {
        test_fail("undo steps failed, status %d, error: %s", status, ai->error);
    }

    struct state * restrict const check_state = create_state(geometry);

    if (state->active != check_state->active) {
        test_fail("All undo: active expected %d, but value is %d.", check_state->active, state->active);
    }

    if (state->ball != check_state->ball) {
        test_fail("All undo: ball expected %d, but value is %d.", check_state->ball, state->ball);
    }

    if (memcmp(state->lines, check_state->lines, geometry->qpoints) != 0) {
        test_fail("All undo: lines mismatch.");
    }

    ai->free(ai);
    destroy_state(check_state);

    destroy_geometry(geometry);
    return 0;
}

#endif
