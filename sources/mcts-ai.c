#include "paper-football.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

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
    struct step_stat stats[QSTEPS];

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
    struct ai_explanation * restrict const explanation);

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
    free_state(me->state);
    free_state(me->backup);
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

    init_state(state, geometry, lines);
    init_state(backup, geometry, backup_lines);
    return me;
}

void free_mcts_ai(struct ai * restrict const ai)
{
    free_history(&ai->history);
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

    struct history * restrict const history = &ai->history;
    const int status = history_push(history, me->state);
    if (status != 0) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "Bad history push, return code is %d.", status);
        return status;
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

    struct history * restrict const history = &ai->history;
    const unsigned int old_qstep_changes = history->qstep_changes;

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
            history->qstep_changes = old_qstep_changes;
            return EINVAL;
        }

        const int status = history_push(history, me->state);
        if (status != 0) {
            const int index = ptr - steps;
            snprintf(me->error_buf, ERROR_BUF_SZ, "Bad history push on step %d, return code is %d.", index, status);
            ai->error = me->error_buf;
            restore_backup(me);
            history->qstep_changes = old_qstep_changes;
            return status;
        }
    }

    return 0;
}

int mcts_ai_undo_steps(
    struct ai * restrict const ai,
    unsigned int qsteps)
{
    if (qsteps == 0) {
        return 0;
    }

    ai->error = NULL;
    struct mcts_ai * restrict const me = ai->data;

    struct history * restrict const history = &ai->history;
    if (history->qstep_changes == 0) {
        return EINVAL;
    }

    const struct step_change * last_change = history->step_changes + history->qstep_changes;
    const int what = last_change[-1].what;
    if (what != CHANGE_PASS && what != CHANGE_FREE_KICK) {
        return EINVAL;
    }

    --qsteps;

    const struct step_change * ptr = last_change - 1;
    const struct step_change * const end = history->step_changes;
    for (;;) {
        if (ptr == end) break;
        const int what = ptr[-1].what;
        if (what == CHANGE_PASS || what == CHANGE_FREE_KICK) {
            if (qsteps == 0) {
                break;
            }
            --qsteps;
        }
        --ptr;
    }

    const unsigned int qstep_changes = last_change - ptr;
    state_rollback(me->state, ptr, qstep_changes);
    history->qstep_changes -= qstep_changes;
    return 0;
}

int mcts_ai_undo_step(struct ai * restrict const ai)
{
    return mcts_ai_undo_steps(ai, 1);
}

enum step mcts_ai_go(
    struct ai * restrict const ai,
    struct ai_explanation * restrict const explanation)
{
    ai->error = NULL;
    struct mcts_ai * restrict const me = ai->data;
    const enum step step = ai_go(me, explanation);
    if (step == INVALID_STEP) {
        ai->error = me->error_buf;
    }
    return step;
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

const struct state * mcts_ai_get_state(const struct ai * const ai)
{
    struct mcts_ai * restrict const me = ai->data;
    return me->state;
}

int init_mcts_ai(
    struct ai * restrict const ai,
    const struct geometry * const geometry)
{
    ai->error = NULL;

    if (geometry == NULL) {
        ai->error = "Argument “geometry” cannot be NULL.";
        return EINVAL;
    }

    ai->data = create_mcts_ai(geometry);
    if (ai->data == NULL) {
        ai->error = "Bad alloc for create_mcts_ai.";
        return errno;
    }

    init_history(&ai->history);

    ai->reset = mcts_ai_reset;
    ai->do_step = mcts_ai_do_step;
    ai->do_steps = mcts_ai_do_steps;
    ai->undo_step = mcts_ai_undo_step;
    ai->undo_steps = mcts_ai_undo_steps;
    ai->go = mcts_ai_go;
    ai->get_params = mcts_ai_get_params;
    ai->set_param = mcts_ai_set_param;
    ai->get_state = mcts_ai_get_state;
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

static struct node * alloc_node(struct mcts_ai * restrict const me)
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

static inline enum step random_step(steps_t steps)
{
    enum step alternatives[QSTEPS];
    int qalternatives = 0;
    for (enum step step=0; step<QSTEPS; ++step) {
        const steps_t mask = 1 << step;
        if (mask & steps) {
            alternatives[qalternatives++] = step;
        }
    }
    const int choice = rand() % qalternatives;
    return alternatives[choice];
}

static int rollout(
    struct state * restrict const state,
    uint32_t max_steps,
    uint32_t * qthink)
{
    for (;;) {
        const int status = state_status(state);

        if (status == WIN_1) {
            return +1;
        }

        if (status == WIN_2) {
            return -1;
        }

        if (max_steps-- == 0) {
            return 0;
        }

        steps_t answers = state_get_steps(state);
        if (answers == 0) {
            return state->active != 1 ? +1 : -1;
        }

        const int multiple_ways = answers & (answers - 1);
        const enum step step = multiple_ways ? random_step(answers) : first_step(answers);

        state_step(state, step);
        ++*qthink;
    }
}

static void update_history(
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

static void add_history(
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

static enum step select_step(
    const struct mcts_ai * const me,
    const struct node * const node,
    steps_t steps)
{
    int qbest = 0;
    enum step best_steps[QSTEPS];
    float best_weight = -1.0e+10f;

    const int multiple_ways = steps & (steps - 1);
    if (!multiple_ways) {
        const enum step choice = first_step(steps);
        return choice;
    }

    const float total = node->qgames;
    const float log_total = log(total);
    while (steps != 0) {
        const enum step step = extract_step(&steps);
        const struct node * const child = me->nodes + node->children[step];
        const float score = child->score;
        const float qgames = child->qgames;
        const float ev = score / qgames;
        const float investigation = sqrt(log_total/qgames);
        const float weight = ev + me->C * investigation;

        if (weight >= best_weight) {
            if (weight != best_weight) {
                qbest = 0;
                best_weight = weight;
            }
            best_steps[qbest++] = step;
        }
    }

    const int index = qbest == 1 ? 0 : rand() % qbest;
    const enum step choice = best_steps[index];
    return choice;
}

static uint32_t simulate(
    struct mcts_ai * restrict const me,
    struct node * restrict node)
{
    struct state * restrict const state = me->backup;
    state_copy(state, me->state);

    if (state->ball == GOAL_1) {
        return 1;
    }

    if (state->ball == GOAL_2) {
        return 1;
    }

    uint32_t qthink = 1;
    me->hist_ptr = me->hist;

    for (;;) {

        steps_t answers = state_get_steps(state);
        if (answers == 0) {
            update_history(me, state->active != 1 ? +1 : -1);
            return qthink;
        }

        const enum step step = select_step(me, node, answers);
        ++qthink;

        uint32_t ichild = node->children[step];
        if (ichild == 0) {
            struct node * restrict const child = alloc_node(me);
            if (child == NULL) {
                return 0;
            }
            node->children[step] = child - me->nodes;
            node = child;
        } else {
            node = me->nodes + ichild;
        }

        add_history(me, node, state->active);

        state_step(state, step);
        const int status = state_status(state);

        if (status == WIN_1) {
            update_history(me, +1);
            return qthink;
        }

        if (status == WIN_2) {
            update_history(me, -1);
            return qthink;
        }

        if (ichild == 0) {
            break;
        }
    }

    const int32_t score = rollout(state, me->max_depth, &qthink);
    update_history(me, score);
    return qthink;
}

static int compare_stats(
    const void * const ptr_a,
    const void * const ptr_b)
{
    const struct step_stat * a = ptr_a;
    const struct step_stat * b = ptr_b;
    if (a->qgames > b->qgames) return -1;
    if (a->qgames < b->qgames) return +1;
    return 0;
}

static enum step ai_go(
    struct mcts_ai * restrict const me,
    struct ai_explanation * restrict const explanation)
{
    if (explanation) {
        explanation->qstats = 0;
        explanation->stats = NULL;
        explanation->time = 0.0;
        explanation->score = -1.0;
    }

    const steps_t steps = state_get_steps(me->state);
    if (steps == 0) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "no possible steps.");
        return INVALID_STEP;
    }

    const int multiple_ways = steps & (steps - 1);
    if (!multiple_ways) {
        const enum step choice = first_step(steps);
        return choice;
    }

	double start = clock();

    init_cache(me);

    struct node * restrict const zero = alloc_node(me);
    if (zero == NULL) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "alloc zero node failed.");
        return INVALID_STEP;
    }
    zero->score = 2;
    zero->qgames = 1;

    struct node * restrict const root = alloc_node(me);
    if (root == NULL) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "alloc root node failed.");
        return INVALID_STEP;
    }

    root->qgames = 1;
    uint32_t qthink = 0;
    for (;;) {
        const uint32_t delta_think = simulate(me, root);
        if (delta_think == 0) {
            break;
        }

        qthink += delta_think;
        ++root->qgames;

        if (qthink >= me->qthink) {
            break;
        }
    }

    int qbest = 0;
    int32_t best_qgames = 0;
    enum step best_steps[QSTEPS];

    for (enum step step=0; step<QSTEPS; ++step) {
        const uint32_t ichild = root->children[step];
        if (ichild == 0) {
            continue;
        }

        const struct node * const child = me->nodes + ichild;
        if (child->qgames >= best_qgames) {
            if (child->qgames > best_qgames) {
                qbest = 0;
                best_qgames = child->qgames;
            }
            best_steps[qbest++] = step;
        }
    }

    const int index = qbest == 1 ? 0 : rand() % qbest;
	enum step result = best_steps[index];

	if (explanation) {
        double finish = clock();
        explanation->time = (finish - start) / CLOCKS_PER_SEC;

        size_t qstats = 1;
        for (enum step step=0; step<QSTEPS; ++step) {
            const uint32_t ichild = root->children[step];
            if (ichild == 0) {
                continue;
            }

            const struct node * const child = me->nodes + ichild;
            const int32_t qgames = child->qgames;
            const int32_t score = child->score;
            double norm_score = -1.0;
            if (qgames > 0) {
                norm_score = 0.5 * (score + qgames) / (double)qgames;
            }

            const size_t i = step == result ? 0 : qstats;
            me->stats[i].step = step;
            me->stats[i].qgames = child->qgames;
            me->stats[i].score = norm_score;
            qstats += !!i;
        }

        explanation->qstats = qstats;
        explanation->stats = me->stats;

        explanation->score = me->stats[0].score;
        if (me->state->active == 2) {
            explanation->score = 1.0 - explanation->score;
        }

        if (qstats > 2) {
            qsort(me->stats + 1, qstats - 1, sizeof(struct step_stat), compare_stats);
        }
    }

    return result;
}



#ifdef MAKE_CHECK

#include "insider.h"

#define BW   15
#define BH   23
#define GW    4
#define FK    5

#define QROLLOUTS   1024

int test_rollout(void)
{
    init_magic_steps();

    struct geometry * restrict const geometry = create_std_geometry(BW, BH, GW, FK);
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
    struct geometry * restrict const geometry = create_std_geometry(BW, BH, GW, FK);
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

#define HISTORY_QITEMS 1000

int test_mcts_history(void)
{
    struct geometry * restrict const geometry = create_std_geometry(BW, BH, GW, FK);
    if (geometry == NULL) {
        test_fail("create_std_geometry(%d, %d, %d) fails, return value is NULL, errno is %d.",
            BW, BH, GW, errno);
    }

    struct ai storage;
    struct ai * restrict const ai = &storage;
    init_mcts_ai(ai, geometry);
    struct mcts_ai * restrict const me = ai->data;

    const uint32_t cache = (HISTORY_QITEMS + 16) * sizeof(struct node);
    ai->set_param(ai, "cache", &cache);
    init_cache(me);

    const struct node * nodes[HISTORY_QITEMS];

    for (int i=0; i<HISTORY_QITEMS; ++i) {
        struct node * restrict const node = alloc_node(me);
        nodes[i] = node;

        const int active = (i%2) + 1;
        node->qgames = i;
        node->score = active == 1 ? i/2 : -i/2;
        add_history(me, node, active);
    }

    update_history(me, -1);

    for (int i=0; i<HISTORY_QITEMS; ++i) {
        const struct node * const node = nodes[i];
        if (node->qgames != i+1) {
            test_fail("Unexpected qgames %u for nodes[%d], %d expected.", node->qgames, i, i+1);
        }
        const int active = (i%2) + 1;
        const int32_t score = active == 1 ? i/2 - 1 : 1 - i/2;
        if (node->score != score) {
            test_fail("Unexpected score %d for nodes[%d], %d expected.", node->score, i, score);
        }
    }

    ai->free(ai);
    destroy_geometry(geometry);
    return 0;
}

int test_ucb_formula(void)
{
    struct geometry * restrict const geometry = create_std_geometry(BW, BH, GW, FK);
    if (geometry == NULL) {
        test_fail("create_std_geometry(%d, %d, %d) fails, return value is NULL, errno is %d.",
            BW, BH, GW, errno);
    }

    struct ai storage;
    struct ai * restrict const ai = &storage;
    init_mcts_ai(ai, geometry);
    const uint32_t cache = 1024 * sizeof(struct node);
    const int status = ai->set_param(ai, "cache", &cache);
    if (status != 0) {
        test_fail("ai->set_param fails with code %d, %s.", status, ai->error);
    }

    struct mcts_ai * restrict const me = ai->data;
    init_cache(me);

    struct node node;
    node.qgames = 10;
    node.score = 0;

    node.children[NORTH] = 1; /*       1.55985508 */
    node.children[EAST]  = 2; /* BEST  1.56219899 */
    node.children[SOUTH] = 3; /*       1.55005966 */
    node.children[WEST]  = 4; /*       1.53394851 */

    me->C = 1.4;

    me->nodes[1].qgames = 3;
    me->nodes[2].qgames = 4;
    me->nodes[3].qgames = 5;
    me->nodes[4].qgames = 6;

    me->nodes[1].score = 1;
    me->nodes[2].score = 2;
    me->nodes[3].score = 3;
    me->nodes[4].score = 4;

    steps_t steps = (1 << NORTH) | (1 << EAST) | (1 << SOUTH) | (1 << WEST);
    const enum step choice = select_step(me, &node, steps);

    if (choice != EAST) {
        test_fail("Unexpected choice %d, expected EAST (%d).", choice, EAST);
    }

    struct node * restrict const root = alloc_node(me);
    if (root == NULL) {
        test_fail("alloc_node failed with NULL as a return value for root node.");
    }
    root->qgames = 1;

    for (enum step step=0; step<QSTEPS; ++step) {
        struct node * restrict const child = alloc_node(me);
        if (child == NULL) {
            test_fail("alloc_node failed with NULL as a return value for child node on step %d.", step);
        }
        child->qgames = 1;
        child->score = 2;
        root->children[step] = child - me->nodes;
    }

    steps_t visited = 0;
    for (enum step step=0; step<QSTEPS; ++step) {
        const enum step choice = select_step(me, root, 0xFF);
        visited |= 1 << choice;
        struct node * restrict const child = me->nodes + root->children[choice];
        child->qgames = 1;
        child->score = (rand() % 3) - 1;
        ++root->qgames;
    }

    if (visited != 0xFF) {
        test_fail("Some directions are visitied twice, visited mask is 0x%02X.", visited);
    }

    ai->free(ai);
    destroy_geometry(geometry);
    return 0;
}

#define QSIMULATIONS  1000

int test_simulation(void)
{
    struct geometry * restrict const geometry = create_std_geometry(BW, BH, GW, FK);
    if (geometry == NULL) {
        test_fail("create_std_geometry(%d, %d, %d) fails, return value is NULL, errno is %d.",
            BW, BH, GW, errno);
    }

    struct ai storage;
    struct ai * restrict const ai = &storage;
    init_mcts_ai(ai, geometry);
    const uint32_t cache = 2 * QSIMULATIONS * sizeof(struct node);
    ai->set_param(ai, "cache", &cache);

    struct mcts_ai * restrict const me = ai->data;
    init_cache(me);

    struct node * restrict const zero = alloc_node(me);
    if (zero == NULL) {
        test_fail("alloc zero node failed.");
    }
    zero->score = 2;
    zero->qgames = 1;

    struct node * restrict const root = alloc_node(me);
    if (zero == NULL) {
        test_fail("alloc root node failed.");
    }

    root->qgames = 1;
    for (int i=0; i<QSIMULATIONS; ++i) {
        simulate(me, root);
        ++root->qgames;
    }

    if (root->qgames != QSIMULATIONS + 1) {
        test_fail("root->qgames = %u, but %u expected.", root->qgames, QSIMULATIONS);
    }

    ai->free(ai);
    destroy_geometry(geometry);
    return 0;
}

int test_mcts_ai_unstep(void)
{
    int status;

    struct geometry * restrict const geometry = create_std_geometry(BW, BH, GW, FK);
    if (geometry == NULL) {
        test_fail("create_std_geometry(%d, %d, %d) fails, return value is NULL, errno is %d.",
            BW, BH, GW, errno);
    }

    struct ai storage;
    struct ai * restrict const ai = &storage;

    init_mcts_ai(ai, geometry);

    const uint32_t qthink = 2 * 1024;
    status = ai->set_param(ai, "qthink", &qthink);
    if (status != 0) {
        test_fail("ai->set_param fails with code %d, %s.", status, ai->error);
    }

    const uint32_t cache = 2 * qthink;
    status = ai->set_param(ai, "cache", &cache);
    if (status != 0) {
        test_fail("ai->set_param fails with code %d, %s.", status, ai->error);
    }

    unsigned int qsteps = 0;
    const struct state * const state = ai->get_state(ai);
    while (state_status(state) == IN_PROGRESS) {
        const enum step step = ai->go(ai, NULL);
        ai->do_step(ai, step);
        ++qsteps;
    }

    status = ai->undo_steps(ai, qsteps);
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
