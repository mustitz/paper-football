#include "paper-football.h"

#include <stdio.h>

#define ERROR_BUF_SZ   256

#define QPARAMS   4

static const uint32_t     def_cache = 2 * 1024 * 1024;
static const uint32_t    def_qthink =     1024 * 1024;
static const uint32_t def_max_depth =             128;
static const  float           def_C =             1.4;

struct mcts_ai
{
    struct state * state;
    struct state * backup;
    char * error_buf;
    struct ai_param params[QPARAMS+1];

    uint32_t cache;
    uint32_t qthink;
    uint32_t max_depth;
    float    C;
};

#define OFFSET(name) offsetof(struct mcts_ai, name)
static struct ai_param def_params[QPARAMS+1] = {
    {     "cache",     &def_cache, U32, OFFSET(cache) },
    {    "qthink",    &def_qthink, U32, OFFSET(qthink) },
    { "max_depth", &def_max_depth, U32, OFFSET(max_depth) },
    {         "C",         &def_C, F32, OFFSET(C) },
    { NULL, NULL, NO_TYPE, 0 }
};

static void * move_ptr(void * ptr, size_t offset)
{
    char * restrict const base = ptr;
    return base + offset;
}

static int set_param(
    struct mcts_ai * restrict const me,
    const struct ai_param * const param,
    const void * const value)
{
    const size_t sz = param_sizes[param->type];
    if (sz == 0) {
        return EINVAL;
    }

    void * restrict const ptr = move_ptr(me, param->offset);
    memcpy(ptr, value, sz);
    return 0;
}

static void init_param(
    struct mcts_ai * restrict const me,
    const int index)
{
    const struct ai_param * const def_param = def_params + index;
    struct ai_param * restrict const param = me->params + index;
    param->value = move_ptr(me, param->offset);
    set_param(me, param, def_param->value);
}

static void free_ai(struct mcts_ai * restrict const me)
{
    free(me);
}

struct mcts_ai * create_mcts_ai(const struct geometry * const geometry)
{
    const uint32_t qpoints = geometry->qpoints;
    const size_t sizes[6] = {
        sizeof(struct mcts_ai),
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

    struct mcts_ai * restrict const me = data;
    struct state * restrict const state = ptrs[1];
    uint8_t * restrict const lines = ptrs[2];
    struct state * restrict const backup = ptrs[3];
    uint8_t * restrict const backup_lines = ptrs[4];
    char * const error_buf = ptrs[5];

    me->state = state;
    me->backup = backup;
    me->error_buf = error_buf;

    memcpy(me->params, def_params, sizeof(me->params));
    for (int i=0; i<QPARAMS; ++i) {
        init_param(me, i);
    }

    state->geometry = geometry;
    state->lines = lines;
    state->active = 1;
    state->ball = qpoints / 2;

    backup->geometry = geometry;
    backup->lines = backup_lines;

    init_lines(geometry, lines);
    return me;
}

void free_mcts_ai(struct ai * restrict const ai)
{
    free_ai(ai->data);
}

int mcts_ai_reset(
    struct ai * restrict const ai,
    const struct geometry * const geometry)
{
    ai->error = NULL;

    struct mcts_ai * restrict const me = create_mcts_ai(geometry);
    if (me == NULL) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "Bad alloc for create_mcts_ai.");
        ai->error = me->error_buf;
        return errno;
    }

    const struct ai_param * ptr = ai->get_params(ai);
    for (; ptr->name != NULL; ++ptr) {
        const int status = set_param(me, ptr, ptr->value);
        if (status != 0) {
            snprintf(me->error_buf, ERROR_BUF_SZ, "Cannot set parameter %s for new instance, status is %d.", ptr->name, status);
            ai->error = me->error_buf;
            free_ai(me);
            return status;
        }
    }

    free_ai(ai->data);
    ai->data = me;
    return 0;
}

int mcts_ai_do_step(
    struct ai * restrict const ai,
    const enum step step)
{
    ai->error = NULL;
    struct mcts_ai * restrict const me = ai->data;
    const int next = state_step(me->state, step);

    if (next == NO_WAY) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "Direction occupied.");
        ai->error = me->error_buf;
        return EINVAL;
    }

    return 0;
}

static void restore_backup(struct mcts_ai * restrict const me)
{
    struct state * old_state = me->state;
    me->state = me->backup;
    me->backup = old_state;
}

int mcts_ai_do_steps(
    struct ai * restrict const ai,
    const unsigned int qsteps,
    const enum step steps[])
{
    ai->error = NULL;
    struct mcts_ai * restrict const me = ai->data;

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

enum step mcts_ai_go(
    struct ai * restrict const ai,
    struct step_stat * restrict const stats)
{
    ai->error = NULL;
    struct mcts_ai * restrict const me = ai->data;

    steps_t steps = state_get_steps(me->state);
    if (steps == 0) {
        errno = EINVAL;
        return INVALID_STEP;
    }

    return first_step(steps);
}

const struct ai_param * mcts_ai_get_params(const struct ai * const ai)
{
    struct mcts_ai * restrict const me = ai->data;
    return me->params;
}

static const struct ai_param * find_param(
    struct mcts_ai * restrict const me,
    const char * const name)
{
    for (int i=0; i<QPARAMS; ++i) {
        const struct ai_param * const param = me->params + i;
        if (strcasecmp(name, param->name) == 0) {
            return param;
        }
    }

    return NULL;
}

int mcts_ai_set_param(
    struct ai * restrict const ai,
    const char * const name,
    const void * const value)
{
    struct mcts_ai * restrict const me = ai->data;
    const struct ai_param * const param = find_param(me, name);
    if (param == NULL) {
        return EINVAL;
    }

    set_param(me, param, value);
    return 0;
}

int init_mcts_ai(
    struct ai * restrict const ai,
    const struct geometry * const geometry)
{
    ai->error = NULL;

    ai->data = create_mcts_ai(geometry);
    if (ai->data == NULL) {
        ai->error = "Bad alloc for create_mcts_ai.";
        return errno;
    }

    ai->reset = mcts_ai_reset;
    ai->do_step = mcts_ai_do_step;
    ai->do_steps = mcts_ai_do_steps;
    ai->go = mcts_ai_go;
    ai->get_params = mcts_ai_get_params;
    ai->set_param = mcts_ai_set_param;
    ai->free = free_mcts_ai;
    return 0;
}
