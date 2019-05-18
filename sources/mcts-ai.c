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

    struct node * nodes;
    uint32_t total_nodes;
    uint32_t used_nodes;
    uint32_t good_node_alloc;
    uint32_t bad_node_alloc;

    struct hist_item * hist;
    struct hist_item * hist_ptr;
    struct hist_item * hist_last;
    uint32_t max_hist_len;
};

struct hist_item
{
    uint32_t inode;
    int active;
};

struct node
{
    int32_t score;
    int32_t qgames;
    int32_t children[QSTEPS];
};

static void init_magic_steps(void);
static enum step ai_go(
    struct mcts_ai * restrict const me,
    struct step_stat * restrict const stats);

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

static void reset_cache(struct mcts_ai * restrict const me)
{
    me->total_nodes = me->nodes ? me->cache / sizeof(struct node) : 0;
    me->used_nodes = 0;
    me->good_node_alloc = 0;
    me->bad_node_alloc = 0;
}

static void free_cache(struct mcts_ai * restrict const me)
{
    if (me->nodes) {
        free(me->nodes);
        me->nodes = NULL;
    }

    reset_cache(me);
}

static int set_cache(
    struct mcts_ai * restrict const me,
    const uint32_t * value)
{
    const unsigned int node_sz = sizeof(struct node);
    const unsigned int min_cache_sz = 16 * node_sz;
    if (*value < min_cache_sz) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "Too small value for cache, minimum is %u.", min_cache_sz);
        return EINVAL;
    }

    free_cache(me);
    return 0;
}

static int init_cache(struct mcts_ai * restrict const me)
{
    if (me->nodes == NULL && me->cache > 0) {
        me->nodes = malloc(me->cache);
        if (me->nodes == NULL) {
            snprintf(me->error_buf, ERROR_BUF_SZ, "Bad alloc %u bytes (nodes).", me->cache);
            return ENOMEM;
        }
    }

    reset_cache(me);
    return 0;
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

    int status = 0;
    switch (param->offset) {
        case OFFSET(cache):
            status = set_cache(me, value);
            break;
    }

    if (status == 0) {
        void * restrict const ptr = move_ptr(me, param->offset);
        memcpy(ptr, value, sz);
    }

    return status;
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
    free_cache(me);
    if (me->hist) {
        free(me->hist);
    }
    free(me);
}

struct mcts_ai * create_mcts_ai(const struct geometry * const geometry)
{
    init_magic_steps();

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

    me->nodes = NULL;
    reset_cache(me);

    me->hist = NULL;
    me->hist_last = NULL;
    me->hist_ptr = NULL;
    me->max_hist_len = 0;

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
    return ai_go(me, stats);
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
    ai->error = NULL;

    struct mcts_ai * restrict const me = ai->data;
    const struct ai_param * const param = find_param(me, name);
    if (param == NULL) {
        return EINVAL;
    }

    const int status = set_param(me, param, value);
    if (status != 0) {
        ai->error = me->error_buf;
    }
    return status;
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



/* AI step selection */

static enum step magic_steps[256][8];

static void init_magic_steps(void)
{
    if (magic_steps[1][1] == 1) {
        return;
    }

    for (uint32_t mask=0; mask<256; ++mask) {
        steps_t steps = mask;
        for (int n=0; n<8; ++n) {
            if (steps == 0) {
                magic_steps[mask][n] = INVALID_STEP;
            } else {
                enum step step = extract_step(&steps);
                magic_steps[mask][n] = step;
            }
        }
    }
}

struct node * alloc_node(struct mcts_ai * restrict const me)
{
    if (me->used_nodes >= me->total_nodes) {
        ++me->bad_node_alloc;
        return NULL;
    }

    struct node * restrict const result = me->nodes + me->used_nodes;
    ++me->good_node_alloc;
    ++me->used_nodes;
    memset(result, 0, sizeof(struct node));
    return result;
}

int rollout(
    struct state * restrict const state,
    uint32_t max_steps,
    uint32_t * qthink)
{
    const int32_t * const connections = state->geometry->connections;

    int active = state->active;
    int ball = state->ball;
    uint8_t * restrict const lines = state->lines;

    if (ball == GOAL_1) {
        return +1;
    }

    if (ball == GOAL_2) {
        return -1;
    }

    for (;;) {
        if (max_steps-- == 0) {
            return 0;
        }

        const steps_t ball_lines = lines[ball];
        const steps_t answers = ball_lines ^ 0xFF;
        if (answers == 0) {
            return active != 1 ? +1 : -1;
        }

        const int qanswers = step_count(answers);
        const int index = qanswers == 1 ? 0 : rand() % qanswers;
        enum step step = magic_steps[answers][index];

        const int next = connections[ball*QSTEPS + step];

        if (next == GOAL_1) {
            return +1;
        }

        if (next == GOAL_2) {
            return -1;
        }

        lines[ball] |= (1 << step);
        lines[next] |= (1 << BACK(step));
        ball = next;
        ++*qthink;

        if (ball_lines == 0) {
            active ^= 3;
        }
    }
}

void update_history(
    struct mcts_ai * restrict const me,
    const int32_t score)
{
    const struct hist_item * ptr = me->hist;
    const struct hist_item * const end = me->hist_ptr;
    for (; ptr != end; ++ptr) {
        struct node * restrict const node = me->nodes + ptr->inode;
        ++node->qgames;
        node->score += ptr->active == 1 ? score : -score;
    }

    const uint32_t hist_len = me->hist_ptr - me->hist;
    if (hist_len > me->max_hist_len) {
        me->max_hist_len = hist_len;
    }
}

void add_history(
    struct mcts_ai * restrict const me,
    struct node * restrict const node,
    const int active)
{
    if (me->hist_ptr != me->hist_last) {
        me->hist_ptr->inode = node - me->nodes;
        me->hist_ptr->active = active;
        ++me->hist_ptr;
        return;
    }

    const size_t hist_capacity = me->hist_last - me->hist;
    const size_t new_hist_capacity = 128 + 2 * hist_capacity;
    const size_t new_history_sz = new_hist_capacity * sizeof(struct hist_item);
    struct hist_item * restrict const new_hist = realloc(me->hist, new_history_sz);
    if (new_hist == NULL) {
        return;
    }

    me->hist_ptr += new_hist - me->hist;
    me->hist = new_hist;
    me->hist_last = new_hist + new_hist_capacity;
    me->hist_ptr->inode = node - me->nodes;
    me->hist_ptr->active = active;
    ++me->hist_ptr;
}

static enum step ai_go(
    struct mcts_ai * restrict const me,
    struct step_stat * restrict const stats)
{
    init_cache(me);

    steps_t steps = state_get_steps(me->state);
    if (steps == 0) {
        errno = EINVAL;
        return INVALID_STEP;
    }

    return first_step(steps);
}



#ifdef MAKE_CHECK

#include "insider.h"

#define BW    9
#define BH   11
#define GW    2

#define QROLLOUTS   1024

int test_rollout(void)
{
    init_magic_steps();

    struct geometry * restrict const geometry = create_std_geometry(BW, BH, GW);
    if (geometry == NULL) {
        test_fail("create_std_geometry(%d, %d, %d) fails, return value is NULL, errno is %d.",
            BW, BH, GW, errno);
    }

    struct state * restrict const state = create_state(geometry);
    if (state == NULL) {
        test_fail("create_state(geometry) fails, fails, return value is NULL, errno is %d.", errno);
    }

    struct state * restrict const base = create_state(geometry);
    if (base == NULL) {
        test_fail("create_state(geometry) fails, fails, return value is NULL, errno is %d.", errno);
    }

    for (int i=0; i<QROLLOUTS; ++i) {
        state_copy(state, base);

        uint32_t qthink = 0;
        const int score = rollout(state, BW*BH*8, &qthink);
        if (score != -1 && score != +1) {
            test_fail("rollout %d returns unexpected score %d (-1 or +1 expected).", i, score);
        }

        if (qthink >= BW*BH*8) {
            test_fail("Unexpected qthink value %u after rollout.", qthink);
        }
    }

    state_copy(state, base);
    uint32_t qthink = 0;
    const int score = rollout(state, 4, &qthink);
    if (score != 0) {
        test_fail("short rollout returns unexpected score %d, 0 expected.", score);
    }

    if (qthink != 4) {
        test_fail("Unexpected qthink value %u after rollout, 4 expected.", qthink);
    }

    destroy_state(base);
    destroy_state(state);
    destroy_geometry(geometry);
    return 0;
}

#define ALLOCATED_NODES    32

int test_node_cache(void)
{
    struct geometry * restrict const geometry = create_std_geometry(BW, BH, GW);
    if (geometry == NULL) {
        test_fail("create_std_geometry(%d, %d, %d) fails, return value is NULL, errno is %d.",
            BW, BH, GW, errno);
    }

    struct ai storage;
    struct ai * restrict const ai = &storage;
    init_mcts_ai(ai, geometry);

    const uint32_t cache = ALLOCATED_NODES * sizeof(struct node);
    const int status = ai->set_param(ai, "cache", &cache);
    if (status != 0) {
        test_fail("ai->set_param fails with code %d, %s.", status, ai->error);
    }

    struct mcts_ai * restrict const me = ai->data;

    for (int j=0; j<3; ++j) {
        init_cache(me);
        for (unsigned int i=0; i<ALLOCATED_NODES; ++i) {
            struct node * restrict const node = alloc_node(me);
            if (node == NULL) {
                test_fail("%d alloc node fails, NULL is returned.", i);
            }

            if (me->good_node_alloc != i+1) {
                test_fail("good_node_alloc mismatch, actual %u, expected %u.", me->good_node_alloc, i+1);
            }

            if (me->bad_node_alloc != 0) {
                test_fail("bad_node_alloc mismatch, actual %u, expected %u.", me->bad_node_alloc, 0);
            }
        }

        for (unsigned int i=0; i<ALLOCATED_NODES/2; ++i) {
            struct node * restrict const node = alloc_node(me);
            if (node != NULL) {
                test_fail("%d alloc, failture expected, but node is allocated.", i);
            }

            if (me->good_node_alloc != ALLOCATED_NODES) {
                test_fail("good_node_alloc mismatch, actual %u, expected %u.", me->good_node_alloc, ALLOCATED_NODES);
            }

            if (me->bad_node_alloc != i+1) {
                test_fail("bad_node_alloc mismatch, actual %u, expected %u.", me->bad_node_alloc, i+1);
            }
        }

        if (j == 1) {
            free_cache(me);
        }
    }

    ai->free(ai);
    destroy_geometry(geometry);
    return 0;
}

#endif
