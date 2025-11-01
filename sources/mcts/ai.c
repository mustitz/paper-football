#include "paper-football.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

struct mcts_ai;
struct node;
struct exnode;
struct bsf_serie;
struct ball_move;

LOG_FUNC void mcts_log_node(
    const char * name,
    const struct mcts_ai * const me,
    const struct node * const node)
LOG_BODY

LOG_FUNC void mcts_log_exnode(
    const char * name,
    const struct mcts_ai * const me,
    const struct exnode * const exnode)
LOG_BODY

LOG_FUNC void mcts_log_ball_moves(
    const struct ball_move * bm,
    int qballs)
LOG_BODY

LOG_FUNC void mcts_log_snapshot(
    const struct mcts_ai * const me)
LOG_BODY

LOG_FUNC void mcts_log_state(
    const char * name,
    const struct state * const state)
LOG_BODY

#define ERROR_BUF_SZ   256

#define QPARAMS   4

static const uint32_t    def_qthink =          1024 * 1024;
static const uint32_t     def_cache = CACHE_AUTO_CALCULATE;
static const uint32_t def_max_depth =                  128;
static const  float           def_C =                  1.4;

#define EXNODE_CHILDREN (QSTEPS + 4)

struct mcts_ai
{
    struct state * state;
    struct state * backup;
    struct bsf_free_kicks * bsf;
    char * error_buf;
    struct ai_param params[QPARAMS+1];
    struct choice_stat stats[MAX_QANSWERS];
    enum step * explanation_steps;
    struct preparation prep;

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

    struct cycle_guard cycle_guard;
    struct cycle_guard backup_cycle_guard;

    struct warns * warns;
};

struct hist_item
{
    uint32_t inode;
    int active;
};

enum node_type {
    NODE_T, NODE_S, NODE_M, NODE_P
};

static const char * node_types[] = { "T", "S", "M", "P" };

union node_opts
{
    struct {
        unsigned qanswers : QANSWERS_BITS;
        unsigned qsteps : QSTEP_BITS;
        unsigned steps : QSTEPS;
        unsigned type : 2;
        unsigned step : 4;
    };
    uint32_t u32;
};

struct node
{
    int32_t score;
    int32_t qgames;
    union node_opts opts;
    int32_t ball;
    int32_t children[QSTEPS];
};

struct exnode
{
    int32_t children[EXNODE_CHILDREN];
};

static enum step ai_go(
    struct mcts_ai * restrict const me,
    struct ai_explanation * restrict const explanation);

#define OFFSET(name) offsetof(struct mcts_ai, name)
static struct ai_param def_params[QPARAMS+1] = {
    {    "qthink",    &def_qthink, U32, OFFSET(qthink) },
    {     "cache",     &def_cache, U32, OFFSET(cache) },
    { "max_depth", &def_max_depth, U32, OFFSET(max_depth) },
    {         "C",         &def_C, F32, OFFSET(C) },
    { NULL, NULL, NO_TYPE, 0 }
};

static const uint32_t MIN_CACHE_SZ = (16 * sizeof(struct node));

static void reset_cache(struct mcts_ai * restrict const me)
{
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

    me->total_nodes = 0;
    reset_cache(me);
}

static int init_cache(struct mcts_ai * restrict const me, unsigned int cache_sz)
{
    free_cache(me);

    if (cache_sz == 0) {
        return 0;
    }

    me->nodes = malloc(cache_sz);
    if (me->nodes == NULL) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "Bad alloc %u bytes (nodes).", me->cache);
        return ENOMEM;
    }

    me->total_nodes = cache_sz / sizeof(struct node);
    reset_cache(me);
    return 0;
}

static void calc_cache(
    struct mcts_ai * restrict const me,
    const uint32_t qthink)
{
    unsigned int cache_sz = qthink;
    unsigned int min_recommended = 1024 * sizeof(struct node);
    if (cache_sz < min_recommended) {
        cache_sz = min_recommended;
    }

    init_cache(me, cache_sz);
}

static int set_cache(
    struct mcts_ai * restrict const me,
    const uint32_t * value)
{
    unsigned int cache_sz = *value;
    if (*value == CACHE_AUTO_CALCULATE) {
        calc_cache(me, me->qthink);
        return 0;
    }

    if (cache_sz < MIN_CACHE_SZ) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "Too small value for cache, minimum is %u.", MIN_CACHE_SZ);
        return EINVAL;
    }

    init_cache(me, cache_sz);
    return 0;
}

static void set_qthink(
    struct mcts_ai * restrict const me,
    const uint32_t * value)
{
    if (me->cache == CACHE_AUTO_CALCULATE) {
        calc_cache(me, *value);
    }
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
        case OFFSET(qthink):
            set_qthink(me, value);
            break;
        case OFFSET(cache):
            status = set_cache(me, value);
            break;
    }

    if (status == 0) {
        void * restrict const ptr = ptr_move(me, param->offset);
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
    param->value = ptr_move(me, param->offset);
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
    destroy_bsf_free_kicks(me->bsf);
    free(me);
}

struct mcts_ai * create_mcts_ai(const struct geometry * const geometry)
{
    struct bsf_free_kicks * bsf = create_bsf_free_kicks(geometry, 1 << QANSWERS_BITS, MAX_FREE_KICK_SERIE, 8, 8);
    if (bsf == NULL) {
        return NULL;
    }

    const uint32_t qpoints = geometry->qpoints;
    const uint32_t free_kick_len = geometry->free_kick_len;
    const uint32_t free_kick_reduce = (free_kick_len - 1) * (free_kick_len - 1);
    const size_t cycle_guard_capacity = 4 + qpoints / free_kick_reduce;
    const size_t sizes[9] = {
        sizeof(struct mcts_ai),
        sizeof(struct state),
        qpoints,
        sizeof(struct state),
        qpoints,
        cycle_guard_capacity * sizeof(struct kick),
        cycle_guard_capacity * sizeof(struct kick),
        MAX_QANSWERS * MAX_FREE_KICK_SERIE * sizeof(enum step),
        ERROR_BUF_SZ
    };

    void * ptrs[9];
    void * data = multialloc(9, sizes, ptrs, 64);

    if (data == NULL) {
        destroy_bsf_free_kicks(bsf);
        return NULL;
    }

    struct mcts_ai * restrict const me = data;
    struct state * restrict const state = ptrs[1];
    uint8_t * restrict const lines = ptrs[2];
    struct state * restrict const backup = ptrs[3];
    uint8_t * restrict const backup_lines = ptrs[4];
    struct kick * restrict cycle_guard_kicks = ptrs[5];
    struct kick * restrict backup_cycle_guard_kicks = ptrs[6];
    enum step * const explanation_steps = ptrs[7];
    char * const error_buf = ptrs[8];

    me->state = state;
    me->backup = backup;
    me->error_buf = error_buf;
    me->bsf = bsf;
    me->explanation_steps = explanation_steps;

    me->nodes = NULL;
    reset_cache(me);

    me->hist = NULL;
    me->hist_last = NULL;
    me->hist_ptr = NULL;
    me->max_hist_len = 0;
    preparation_reset(&me->prep);

    me->cycle_guard.capacity = cycle_guard_capacity;
    me->cycle_guard.kicks = cycle_guard_kicks;
    cycle_guard_reset(&me->cycle_guard);

    me->backup_cycle_guard.capacity = cycle_guard_capacity;
    me->backup_cycle_guard.kicks = backup_cycle_guard_kicks;
    cycle_guard_reset(&me->backup_cycle_guard);

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

static void save_state(
    struct mcts_ai * restrict const me)
{
    state_copy(me->backup, me->state);

    const size_t qkicks = me->cycle_guard.qkicks;
    me->backup_cycle_guard.qkicks = qkicks;
    if (qkicks > 0) {
        const size_t sz = qkicks * sizeof(struct kick);
        memcpy(me->backup_cycle_guard.kicks, me->cycle_guard.kicks, sz);
    }
}

static void restore_backup(struct mcts_ai * restrict const me)
{
    struct state * old_state = me->state;
    me->state = me->backup;
    me->backup = old_state;

    struct kick * old_kicks = me->cycle_guard.kicks;
    me->cycle_guard.kicks = me->backup_cycle_guard.kicks;
    me->backup_cycle_guard.kicks = old_kicks;
    me->cycle_guard.qkicks = me->backup_cycle_guard.qkicks;
}

static steps_t forbid_cycles(
    struct mcts_ai * restrict const me,
    struct cycle_guard * restrict const cycle_guard,
    struct state * restrict state,
    steps_t steps)
{
    steps_t cycles = 0;
    steps_t tmp = steps;
    while (tmp != 0) {
        enum step step = extract_step(&tmp);
        int from = state->ball;
        int to = state->geometry->free_kicks[QSTEPS * from + step];

        enum cycle_result status = cycle_guard_push(cycle_guard, from, to);
        switch (status) {
            case NO_CYCLE:
                cycle_guard_pop(cycle_guard);
                break;
            case CYCLE_FOUND:
                cycles |= 1 << step;
                break;
        }
    }

    if (steps != cycles) {
        return steps ^ cycles;
    }

    WARN(me->warns, STEPS_ARE_CYCLES, "steps", steps, "cycles", cycles);

    static const steps_t player1_priority[QSTEPS] = {
        1 << NORTH,
        1 << NORTH_WEST,
        1 << NORTH_EAST,
        1 << EAST,
        1 << WEST,
        1 << SOUTH_WEST,
        1 << SOUTH_EAST,
        1 << SOUTH,
    };

    static const steps_t player2_priority[QSTEPS] = {
        1 << SOUTH,
        1 << SOUTH_WEST,
        1 << SOUTH_EAST,
        1 << EAST,
        1 << WEST,
        1 << NORTH_WEST,
        1 << NORTH_EAST,
        1 << NORTH,
    };

    const steps_t * priority;
    switch (state->active) {
        case 1:
            priority = player1_priority;
            break;
        case 2:
            priority = player2_priority;
            break;
        default:
            WARN(me->warns, ACTIVE_OOR, "active", state->active, NULL, 0);
            return steps;
    }

    for (int i=0; i<QSTEPS; ++i) {
        steps_t mask = priority[i];
        if (steps & mask) {
            return mask;
        }
    }

    WARN(me->warns, INCONSISTERN_STEPS_PRIORITY, "steps", steps, "active", state->active);
    return steps;
}

static int state_step_proxy(
    struct mcts_ai * restrict const me,
    const enum step step)
{
    struct state * restrict const state = me->state;
    const int old_ball = state->ball;
    const int is_free_kick = is_free_kick_situation(state);

    const int result = state_step(state, step);
    if (result < 0) {
        return result;
    }

    if (is_free_kick) {
        cycle_guard_push(&me->cycle_guard, old_ball, result);
    } else {
        cycle_guard_reset(&me->cycle_guard);
    }

    return result;
}

int mcts_ai_do_step(
    struct ai * restrict const ai,
    const enum step step)
{
    ai->error = NULL;
    struct mcts_ai * restrict const me = ai->data;

    struct preparation * restrict const prep = &me->prep;
    enum step prepared = preparation_pop(prep);
    if (prepared != step) {
        preparation_reset(prep);
    }

    const int next = state_step_proxy(me, step);

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

int mcts_ai_do_steps(
    struct ai * restrict const ai,
    const unsigned int qsteps,
    const enum step steps[])
{
    ai->error = NULL;
    struct mcts_ai * restrict const me = ai->data;

    struct history * restrict const history = &ai->history;
    const unsigned int old_qstep_changes = history->qstep_changes;

    save_state(me);

    const enum step * ptr = steps;
    const enum step * const end = ptr + qsteps;
    for (; ptr != end; ++ptr) {
        const int next = state_step_proxy(me, *ptr);
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

    preparation_reset(&me->prep);

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
    cycle_guard_reset(&me->cycle_guard);
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

    struct mcts_ai * restrict const me = ai->data;
    me->warns = &ai->warns;

    init_history(&ai->history);
    warns_init(&ai->warns);

    ai->reset = mcts_ai_reset;
    ai->do_step = mcts_ai_do_step;
    ai->do_steps = mcts_ai_do_steps;
    ai->undo_step = mcts_ai_undo_step;
    ai->undo_steps = mcts_ai_undo_steps;
    ai->go = mcts_ai_go;
    ai->get_params = mcts_ai_get_params;
    ai->set_param = mcts_ai_set_param;
    ai->get_state = mcts_ai_get_state;
    ai->get_warn = ai_get_warn;
    ai->free = free_mcts_ai;

    return 0;
}



/* AI step selection */

static struct node * alloc_node(
    struct mcts_ai * restrict const me,
    enum node_type type,
    enum step step)
{
    if (me->used_nodes >= me->total_nodes) {
        log_line("Func %s - overflow", __func__);
        ++me->bad_node_alloc;
        return NULL;
    }

    log_line("Func %s - new %s-node %d", __func__, node_types[type], me->used_nodes);
    struct node * restrict const result = me->nodes + me->used_nodes;
    ++me->good_node_alloc;
    ++me->used_nodes;
    memset(result, 0, sizeof(struct node));

    result->opts.type = type;
    result->opts.step = step;
    result->opts.qanswers = BAD_QANSWERS;
    result->ball = NO_WAY;
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

static inline int extra_nodes(int qanswers)
{
    return (qanswers - QSTEPS + EXNODE_CHILDREN - 2) / (EXNODE_CHILDREN - 1);
}

static inline enum step best_step(
    const struct mcts_ai * const me,
    const struct node * const node,
    int answer)
{
    return get_nth_bit(me->state->geometry, node->opts.steps, answer);
}

static inline struct node * get_answer(
    const struct mcts_ai * const me,
    const struct node * const node,
    int answer)
{
    if (answer < 0) {
        /* WARN */
        return NULL;
    }

    const int qanswers = node->opts.qanswers;
    if (answer >= qanswers) {
        /* WARN */
        return NULL;
    }

    const int extra = extra_nodes(qanswers);
    const int q0 = QSTEPS - extra;

    if (answer < q0) {
        const int inode = node->children[answer];
        return me->nodes + inode;
    }

    const int block = (answer - q0) / EXNODE_CHILDREN;
    const int offset = (answer - q0) % EXNODE_CHILDREN;
    const int32_t eindex = node->children[q0 + block];
    const struct exnode * const exnode = (void *) (me->nodes + eindex);
    const int inode = exnode->children[offset];
    return me->nodes + inode;
}

int select_answer(
    const struct mcts_ai * const me,
    const struct node * const node,
    int qanswers)
{
    log_line("Func %s - enter", __func__);
    mcts_log_node("  node", me, node);

    /* Only one answer - return it */
    if (qanswers == 1) {
        log_line("  only one answer, return 0");
        return 0;
    }

    int qbest = 0;
    int best_answers[QSTEPS * EXNODE_CHILDREN];
    float best_weight = -1.0e+10f;

    const int qgames = node->qgames;
    if (qgames <= 0) {
        int result = rand() % qanswers;
        log_line("  clean paren node (free kick) return random %d", result);
        return result;
    }

    const float total = qgames;
    const float log_total = log(total);

    for (int answer = 0; answer < qanswers; ++answer) {
        const struct node * const child = get_answer(me, node, answer);
        if (child == NULL) {
            log_line("  child %d: NULL", answer);
            continue;
        }

        const int ichild = child - me->nodes;
        const float score = child->score;
        const float qgames = child->qgames;

        if (qgames == 0) {
            /* Unexplored node - prioritize it */
            log_line("  child %d (node %d): unexplored, return %d", answer, ichild, answer);
            return answer;
        }

        const float ev = score / qgames;
        const float investigation = sqrt(log_total / qgames);
        const float weight = ev + me->C * investigation;

        log_line("  child %d (node %d): ev=%.4f qgames=%.0f weight=%.4f", answer, ichild, ev, qgames, weight);

        if (weight >= best_weight) {
            if (weight != best_weight) {
                qbest = 0;
                best_weight = weight;
            }
            best_answers[qbest++] = answer;
        }
    }

    if (qbest == 0) {
        /* No valid answers found - return first */
        log_line("  no valid answers, return 0");
        return 0;
    }

    const int index = qbest == 1 ? 0 : rand() % qbest;
    const int result = best_answers[index];
    log_line("  return %d from qbest=%d", result, qbest);
    return result;
}

static int pack_serie(
    struct node * restrict const node,
    const struct bsf_serie * serie)
{
    const int qsteps = serie->qsteps;
    const enum step * const steps = serie->steps;

    if (qsteps > MAX_FREE_KICK_SERIE) {
        /* WARN */
        return 1;
    }

    uint32_t packed = 0;

    for (int i=0; i<qsteps; ++i) {
        packed = (packed << 3) | steps[i];
    }

    node->children[QSTEPS-1] = packed;
    node->opts.qsteps = qsteps;
    return 0;
}

static void unpack_serie(
    const struct node * restrict const node,
    enum step * restrict const steps)
{
    const int qsteps = node->opts.qsteps;
    uint32_t packed = node->children[QSTEPS-1];

    for (int i = qsteps - 1; i >= 0; --i) {
        steps[i] = packed & 7;
        packed >>= 3;
    }
}

static void apply_answer(
    const struct mcts_ai * const me,
    struct state * restrict const state,
    const struct node * const node)
{
    /* For regular steps */
    if (node->opts.type == NODE_S) {
        enum step step = node->opts.step;
        log_line("Step %s", step_names[step]);
        state_step(state, step);
        return;
    }

    /* For ball_move - nothing to apply */
    if (node->opts.type == NODE_M) {
        return;
    }

    /* For path - unpack and apply serie */
    if (node->opts.type == NODE_P) {
        const int qsteps = node->opts.qsteps;
        enum step steps[MAX_FREE_KICK_SERIE];
        unpack_serie(node, steps);

        for (int i = 0; i < qsteps; ++i) {
            enum step step = steps[i];
            log_line("Step %s", step_names[step]);
            state_step(state, step);
        }
    }
}

static int alloc_answers(
    struct mcts_ai * const me,
    struct node * restrict const node,
    int qanswers,
    enum node_type type)
{
    const int max_answers = QSTEPS * EXNODE_CHILDREN;
    if (qanswers > max_answers) {
        /* WARN */
        return 1;
    }

    int extra = extra_nodes(qanswers);
    if (extra < 0 || extra > EXNODE_CHILDREN) {
        /* WARN */
        return 1;
    }

    int32_t * restrict const children = node->children;

    if (extra == 0) {
        for (int i=0; i<qanswers; ++i) {
            struct node * child = alloc_node(me, type, INVALID_STEP);
            if (child == NULL) {
                return 1;
            }

            int32_t ichild = child - me->nodes;
            children[i] = ichild;
        }

        node->opts.qanswers = qanswers;
        return 0;
    }

    const int q0 = QSTEPS - extra;

    struct exnode * exnodes[extra];
    for (int i=0; i<extra; ++i) {
        struct node * node = alloc_node(me, 0, 0);
        if (node == NULL) {
            return 1;
        }

        children[q0 + i] = node - me->nodes;
        exnodes[i] = (void*) node;
    }

    for (int i=0; i<q0; ++i) {
        struct node * child = alloc_node(me, type, INVALID_STEP);
        if (child == NULL) {
            return 1;
        }

        int32_t ichild = child - me->nodes;
        children[i] = ichild;
    }

    int counter = 0;
    for (int i=q0; i<qanswers; ++i) {
        struct node * child = alloc_node(me, type, INVALID_STEP);
        if (child == NULL) {
            return 1;
        }

        int block = counter / EXNODE_CHILDREN;
        int offset = counter % EXNODE_CHILDREN;
        ++counter;

        struct exnode * restrict const exnode = exnodes[block];
        exnode->children[offset] = child - me->nodes;
    }

    for (int i=0; i<extra; ++i) {
        char buf[2] = { '0' + extra, '\0' };
        mcts_log_exnode(buf, me, exnodes[i]);
    }

    node->opts.qanswers = qanswers;
    return 0;
}

struct ball_move
{
    int ball;
    uint32_t distance;
    const struct bsf_serie ** series;
    int count;
};

static int compare_ball_moves(
    const void * const ptr_a,
    const void * const ptr_b)
{
    const struct ball_move * const a = ptr_a;
    const struct ball_move * const b = ptr_b;

    /* Sort by distance (closer to goal first) */
    if (a->distance < b->distance) return -1;
    if (a->distance > b->distance) return +1;
    return 0;
}

static int best_answer(
    const struct mcts_ai * const me,
    const struct node * const node)
{
    const int qanswers = node->opts.qanswers;
    int best_answers[qanswers];

    int qbest = 0;
    int32_t best_qgames = -2147483648;

    for (int i=0; i<qanswers; ++i) {
        const struct node * const child = get_answer(me, node, i);
        if (child == NULL) {
            /* WARN */
            continue;
        }

        int32_t qgames = child->qgames;
        if (qgames >= best_qgames) {
            if (qgames > best_qgames) {
                qbest = 0;
                best_qgames = qgames;
            }
            best_answers[qbest++] = i;
        }
    }

    if (qbest == 0) {
        /* No valid answers found - return first */
        /* WARN */
        return 0;
    }

    const int index = qbest == 1 ? 0 : rand() % qbest;
    return best_answers[index];
}

static int bsf_ball_move(
    struct mcts_ai * const me,
    struct node * restrict const node,
    const struct ball_move * const bm,
    int index)
{
    const int ball = bm->ball;
    const int count = bm->count;
    const struct bsf_serie * const * const sorted = bm->series;

    log_line("Func %s - node=%d index=%d ball=%d count=%d", __func__, node - me->nodes, index, ball, count);
    mcts_log_node("node", me, node);

    if (count < 0 || count >= MAX_QANSWERS) {
        /* WARN */
        log_line("  count out of range");
        return EFAULT;
    }

    int status = alloc_answers(me, node, count, NODE_P);
    if (status != 0) {
        log_line("  alloc_answers failed");
        return ENOMEM;
    }

    for (int i=0; i<count; ++i) {
        struct node * restrict const pnode = get_answer(me, node, i);
        if (pnode == NULL) {
            /* WARN */
            log_line("  pnode %d is NULL", i);
            return EFAULT;
        }

        pack_serie(pnode, sorted[i]);

        log_line("");
        mcts_log_node("pnode", me, pnode);
    }

    node->opts.qanswers = count;
    node->ball = ball;
    return 0;
}

static int compare_series(
    const void * const ptr_a,
    const void * const ptr_b)
{
    const struct bsf_serie * const * const a = ptr_a;
    const struct bsf_serie * const * const b = ptr_b;

    /* Sort by ball (to group series with same destination) */
    return (*a)->ball - (*b)->ball;
}

static int calc_answers(
    struct mcts_ai * restrict const me,
    struct node * restrict const node,
    struct state * restrict const state)
{
    const int qanswers = node->opts.qanswers;
    if (qanswers != BAD_QANSWERS) {
        return qanswers;
    }

    const int is_free_kick = is_free_kick_situation(state);
    if (!is_free_kick) {
        steps_t steps = state_get_steps(state);
        node->opts.steps = steps;
        const int qanswers = step_count(steps);
        node->opts.qanswers = qanswers;
        return qanswers;
    }

    struct bsf_free_kicks * bsf = me->bsf;
    bsf_gen(me->warns, bsf, state, &me->cycle_guard);

    const struct bsf_serie * const win = bsf->win;
    if (win != NULL) {
        log_line("Func %s - found win", __func__);
        struct node * restrict const win_node = alloc_node(me, NODE_M, INVALID_STEP);
        if (win_node == NULL) {
            return BAD_QANSWERS;
        }

        struct node * restrict const pnode = alloc_node(me, NODE_P, INVALID_STEP);
        if (pnode == NULL) {
            return BAD_QANSWERS;
        }

        const int32_t ball = win->ball;

        pack_serie(pnode, bsf->win);
        pnode->opts.qanswers = 0;
        mcts_log_node("pwin", me, win_node);

        win_node->score = 2;
        win_node->qgames = 1;
        win_node->opts.qanswers = 1;
        win_node->ball = ball;
        win_node->children[0] = pnode - me->nodes;
        mcts_log_node("mwin", me, win_node);

        node->children[0] = win_node - me->nodes;
        node->ball = ball;
        node->opts.qanswers = 1;
        return 1;
    }

    log_line("Func %s - found %d series", __func__, bsf->qseries);

    const int qseries = bsf->qseries;
    if (qseries == 0) {
        node->opts.qanswers = 0;
        return 0;
    }

    const struct bsf_serie * sorted[qseries];
    for (int i=0; i<qseries; ++i) {
        sorted[i] = bsf->series + i;
    }
    qsort(sorted, qseries, sizeof(struct bsf_serie *), compare_series);

    /* First pass: calculate count */
    int qballs = 1;
    int ball = sorted[0]->ball;
    for (int i=1; i<qseries; ++i) {
        int current_ball = sorted[i]->ball;
        if (current_ball == ball) {
            continue;
        }

        ++qballs;
        ball = current_ball;
    }

    const int status = alloc_answers(me, node, qballs, NODE_M);
    if (status != 0) {
        log_line("Func %s - alloc_answers failed with code %d", __func__, status);
        return BAD_QANSWERS;
    }

    log_line("");
    mcts_log_node("children", me, node);

    const uint32_t * const dists = state->active == 1
        ? state->geometry->dist_goal1
        : state->geometry->dist_goal2;

    struct ball_move ball_moves[qballs];

    /* Second pass: fill ball_moves */
    int index = 0;
    int from = 0;
    ball = sorted[0]->ball;
    for (int i=1; i<qseries; ++i) {
        int current_ball = sorted[i]->ball;
        if (current_ball == ball) {
            continue;
        }

        ball_moves[index].ball = ball;
        ball_moves[index].distance = dists[ball];
        ball_moves[index].series = sorted + from;
        ball_moves[index].count = i - from;

        ++index;
        from = i;
        ball = current_ball;
    }

    ball_moves[index].ball = ball;
    ball_moves[index].distance = dists[ball];
    ball_moves[index].series = sorted + from;
    ball_moves[index].count = qseries - from;

    /* Sort ball_moves by distance to goal */
    qsort(ball_moves, qballs, sizeof(struct ball_move), compare_ball_moves);
    mcts_log_ball_moves(ball_moves, qballs);

    mcts_log_node("node", me, node);

    if (qballs > MAX_QANSWERS) {
        /* WARN */
        qballs = MAX_QANSWERS;
    }

    /* Create nodes in sorted order */
    for (int i=0; i<qballs; ++i) {
        log_line("Func %s - get_answer %d for node %d", __func__, i, node - me->nodes);
        struct node * restrict const bnode = get_answer(me, node, i);
        if (bnode == NULL) {
            /* WARN */
            return BAD_QANSWERS;
        }
        const int status = bsf_ball_move(me, bnode, ball_moves + i, i);
        if (status != 0) {
            return BAD_QANSWERS;
        }
        mcts_log_node("ballmove", me, bnode);
    }

    mcts_log_node("result", me, node);
    node->opts.qanswers = qballs;
    return qballs;
}

static uint32_t simulate(
    struct mcts_ai * restrict const me,
    struct node * restrict node)
{
    const struct node * const zero = me->nodes;
    struct state * restrict const state = me->backup;
    save_state(me);

    if (state->ball == GOAL_1) {
        return 1;
    }

    if (state->ball == GOAL_2) {
        return 1;
    }

    uint32_t qthink = 1;
    me->hist_ptr = me->hist;

    enum step last_step = INVALID_STEP;
    int last_answer = -1;

    for (;;) {
        log_line("\n\n-------- new simulation iteration ---------------------\n");
        mcts_log_state("current", state);
        mcts_log_node("current", me, node);

        const int active = state->active;

        const int qanswers = calc_answers(me, node, state);
        if (qanswers == BAD_QANSWERS) {
            return 0;
        }

        if (qanswers == 0) {
            log_line("Func %s - no answers available, active=%d", __func__, state->active);
            update_history(me, active != 1 ? +1 : -1);
            return qthink;
        }

        int answer = select_answer(me, node, qanswers);
        log_line("<-- select_answer: result=%d from qanswers=%d\n", answer, qanswers);
        ++qthink;

        struct node * restrict child = get_answer(me, node, answer);

        if (child == NULL) {
            /* WARN */
            return 0;
        }

        log_line("Func %s - next child, index=%d", __func__, child - me->nodes);
        if (child == zero) {
            last_step = best_step(me, node, answer);
            last_answer = answer;
            break;
        }

        apply_answer(me, state, child);
        log_line("Func %s - apply answer %d from node %d", __func__, answer, child - me->nodes);

        add_history(me, child, active);
        log_line("Func %s - push node %d to history, active=%d", __func__, child - me->nodes, active);

        mcts_log_state("next", state);
        enum state_status status = state_status(state);

        if (status == WIN_1) {
            log_line("Func %s - WIN_1 detected", __func__);
            update_history(me, +1);
            return qthink;
        }

        if (status == WIN_2) {
            log_line("Func %s - WIN_2 detected", __func__);
            update_history(me, -1);
            return qthink;
        }

        node = child;
        log_line("iteration done");
    }

    if (last_step == INVALID_STEP) {
        /* WARN */
        return 0;
    }

    if (last_answer < 0) {
        /* WARN */
        return 0;
    }

    const int old_active = state->active;
    const int new_ball = state_step(state, last_step);

    struct node * restrict const child = alloc_node(me, NODE_S, last_step);
    if (child == NULL) {
        log_line("Func %s - out of nodes", __func__);
        return 0;
    }

    child->ball = new_ball;
    node->children[last_answer] = child - me->nodes;
    log_line("Func %s - allocated new child, index=%d", __func__, child - me->nodes);

    add_history(me, child, old_active);
    log_line("Func %s - push node %d to history, active=%d", __func__, child - me->nodes, old_active);

    log_line("\n\n------------- rollout ----------------------------\n");
    mcts_log_state("last", state);
    mcts_log_node("last", me, node);
    const int32_t score = rollout(state, me->max_depth, &qthink);
    log_line("Rollout %s%d", score > 0 ? "+" : "-", score > 0 ? score : -score);

    update_history(me, score);
    log_line("\n\n------------------ snapshot ----------------------\n");
    mcts_log_snapshot(me);
    log_line("\n\n-------- simulation finished ---------------------\n");
    return qthink;
}

static int compare_stats(
    const void * const ptr_a,
    const void * const ptr_b)
{
    const struct choice_stat * a = ptr_a;
    const struct choice_stat * b = ptr_b;
    if (a->qgames > b->qgames) return -1;
    if (a->qgames < b->qgames) return +1;
    return 0;
}

static enum step best_preparation(
    struct mcts_ai * restrict const me,
    const struct node * const mnode)
{
    int ibest = best_answer(me, mnode);
    log_line("Func %s - ibest = %d", __func__, ibest);

    const struct node * const pnode = get_answer(me, mnode, ibest);
    if (pnode == NULL) {
        /* WARN */
        return INVALID_STEP;
    }
    const int qsteps = pnode->opts.qsteps;
    mcts_log_node("pnode", me, pnode);

    struct preparation * restrict const prep = &me->prep;
    prep->qpreps = qsteps;
    prep->current = 0;
    unpack_serie(pnode, prep->preps);

    return preparation_peek(prep);
}


static enum step ai_go(
    struct mcts_ai * restrict const me,
    struct ai_explanation * restrict const explanation)
{
    warns_reset(me->warns);

    if (explanation) {
        explanation->qstats = 0;
        explanation->stats = NULL;
        explanation->time = 0.0;
        explanation->score = -1.0;
        explanation->cache.used = 0;
        explanation->cache.total = 0;
        explanation->cache.good_alloc = 0;
        explanation->cache.bad_alloc = 0;
    }

    struct preparation * restrict const prep = &me->prep;
    enum step prepared = preparation_peek(prep);
    if (prepared != INVALID_STEP) {
        log_line("Func %s - return preparaion %s", __func__, step_names[prepared]);
        return prepared;
    }

    struct state * restrict state = me->state;

    steps_t steps = state_get_steps(state);
    if (steps == 0) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "no possible steps.");
        return INVALID_STEP;
    }

    int multiple_ways = steps & (steps - 1);
    if (multiple_ways) {
        const int is_free_kick = is_free_kick_situation(state);
        if (is_free_kick) {
            steps = forbid_cycles(me, &me->cycle_guard, state, steps);
            multiple_ways = steps & (steps - 1);
        }
    }

    if (!multiple_ways) {
        const enum step choice = first_step(steps);
        return choice;
    }

    double start = clock();

    reset_cache(me);

    struct node * restrict const zero = alloc_node(me, NODE_T, INVALID_STEP);
    if (zero == NULL) {
        snprintf(me->error_buf, ERROR_BUF_SZ, "alloc zero node failed.");
        return INVALID_STEP;
    }
    zero->score = 2;
    zero->qgames = 1;

    struct node * restrict const root = alloc_node(me, NODE_T, INVALID_STEP);
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

        log_line("Func %s - qgames=%d qthink=%d of %d", __func__, root->qgames, qthink, me->qthink);
        if (qthink >= me->qthink) {
            break;
        }
    }

    log_line("\n\n======== ai=>go, choosing answer =================\n");

    mcts_log_node("root", me, root);
    for (int i=0; i<root->opts.qanswers; ++i) {
        const struct node * const child = get_answer(me, root, i);
        if (child != NULL) {
            mcts_log_node("child", me, child);
        } else {
            log_line("Func %s answer[%d] is NULL", __func__, i);
        }
    }

    int best = best_answer(me, root);

    log_line("Func %s best_answer=%d", __func__, best);

    const struct node * const  best_node = get_answer(me, root, best);
    if (best_node == NULL) {
        /* WARN */
        log_line("Func %s best node is null for answer %d", __func__, best);
        return INVALID_STEP;
    }
    mcts_log_node("best", me, best_node);

    const enum node_type best_type = best_node->opts.type;

    enum step result;
    switch (best_type) {
        case NODE_S:
            result = best_node->opts.step;
            break;
        case NODE_M:
            result = best_preparation(me, best_node);
            break;
        default:
            log_line("Func %s unexpected best node type!", __func__);
            return INVALID_STEP;
    }

    if (explanation) {
        double finish = clock();
        explanation->time = (finish - start) / CLOCKS_PER_SEC;

        size_t qstats = 1;
        enum step * restrict explanation_steps = me->explanation_steps;

        for (int i=0; i<root->opts.qanswers; ++i) {
            const struct node * const child = get_answer(me, root, i);
            if (child == NULL) {
                /* WARN */
                continue;
            }

            const enum step step = child->opts.step;
            const int32_t qgames = child->qgames;
            const int32_t score = child->score;
            double norm_score = -1.0;
            if (qgames > 0) {
                norm_score = 0.5 * (score + qgames) / (double)qgames;
            }

            const size_t istat = i == best ? 0 : qstats;
            int qsteps = 0;

            if (child->opts.type == NODE_S) {
                *explanation_steps = step;
                qsteps = 1;
            }

            if (child->opts.type == NODE_M) {
                const int ibest = best_answer(me, child);
                const struct node * const pnode = get_answer(me, child, ibest);
                if (pnode == NULL) {
                    /* WARN */
                    continue;
                }
                qsteps = pnode->opts.qsteps;
                unpack_serie(pnode, explanation_steps);
            }

            struct choice_stat * restrict const stat = me->stats + istat;
            stat->steps = explanation_steps;
            stat->qsteps = qsteps;
            stat->ball = child->ball;
            stat->qgames = child->qgames;
            stat->score = norm_score;

            explanation_steps += qsteps;
            qstats += !!istat;
        }

        explanation->qstats = qstats;
        explanation->stats = me->stats;

        explanation->score = me->stats[0].score;
        if (state->active == 2) {
            explanation->score = 1.0 - explanation->score;
        }

        if (qstats > 2) {
            qsort(me->stats + 1, qstats - 1, sizeof(struct choice_stat), compare_stats);
        }

        // Fill cache statistics in explanation
        explanation->cache.used = me->used_nodes;
        explanation->cache.total = me->total_nodes;
        explanation->cache.good_alloc = me->good_node_alloc;
        explanation->cache.bad_alloc = me->bad_node_alloc;
    }

    return result;
}



#if ENABLE_LOGS

void mcts_log_node(
    const char * title,
    const struct mcts_ai * const me,
    const struct node * const node)
{
    FILE * f = get_flog();
    if (f == NULL) {
        return;
    }

    int indent = 0;
    while (*title == ' ') {
        ++title;
        ++indent;
    }

    const int index = node - me->nodes;
    const int type = node->opts.type;
    const int step = node->opts.step;
    const int qsteps = node->opts.qsteps;
    const int qchildren = type != NODE_P ? QSTEPS : QSTEPS - 1;

    fprintf(f, "%*sNode #%d <%s> score=%d qgames=%d\n",
        indent, "", index, title, node->score, node->qgames);

    fprintf(f, "%*sopts:", indent+2, "");
    fprintf(f, " type=%s", node_types[type]);
    if (step >= 0 && step < QSTEPS) {
        fprintf(f, " step=%s", step_names[step]);
    }
    if (node->opts.has_answers) {
        fprintf(f, " qanswers=%d", node->opts.qanswers);
    }
    fprintf(f, " qsteps=%d", qsteps);
    fprintf(f, " steps=%02X", node->opts.steps);
    fprintf(f, "\n");

    fprintf(f, "%*schildren:", indent+2, "");
    for (int i=0; i<qchildren; ++i) {
        fprintf(f, " %d", node->children[i]);
    }
    fprintf(f, "\n");

    if (type == NODE_P) {
        enum step path[qsteps];
        unpack_serie(node, path);
        fprintf(f, "%*spath:", indent+2, "");
        for (int i=0; i<qsteps; ++i) {
            fprintf(f, " %s", step_names[path[i]]);
        }
        fprintf(f, "\n");
    }
    fprintf(f, "\n");

    fflush(f);
}

void mcts_log_exnode(
    const char * title,
    const struct mcts_ai * const me,
    const struct exnode * const exnode)
{
    FILE * f = get_flog();
    if (f == NULL) {
        return;
    }

    int indent = 0;
    while (*title == ' ') {
        ++title;
        ++indent;
    }

    const int index = (const struct node*) exnode - me->nodes;
    fprintf(f, "%*sExNode #%d <%s>", indent, "", index, title);

    for (int i=0; i<EXNODE_CHILDREN; ++i) {
        fprintf(f, " %d", exnode->children[i]);
    }
    fprintf(f, "\n");

    fflush(f);
}

void mcts_log_serie(int index, const struct bsf_serie * serie)
{
    FILE * f = get_flog();
    if (f == NULL) {
        return;
    }

    fprintf(f, "    [%d] -", index);
    for (int i = 0; i < serie->qsteps; ++i) {
        fprintf(f, " %s", step_names[serie->steps[i]]);
    }
    fprintf(f, "\n");
    fflush(f);
}

void mcts_log_ball_moves(const struct ball_move * ball_moves, int qballs)
{
    log_line("BallMoves - qballs=%d (sorted by distance)", qballs);
    for (int i = 0; i < qballs; ++i) {
        const struct ball_move * bm = &ball_moves[i];
        log_line("  [%d] - ball=%d distance=%u count=%d", i, bm->ball, bm->distance, bm->count);
        for (int j = 0; j < bm->count; ++j) {
            mcts_log_serie(j, bm->series[j]);
        }
    }
    log_line("");
}

void snode_print_steps(const struct node * const snode)
{
    FILE * f = get_flog();
    if (f == NULL) {
        return;
    }

    steps_t steps = snode->opts.steps;
    if (steps == 0) {
        fprintf(f, " steps=0");
        return;
    }

    fprintf(f, " ");
    const enum step step = extract_step(&steps);
    fprintf(f, "%s", step_names[step]);
    while (steps != 0) {
        const enum step step = extract_step(&steps);
        fprintf(f, "|%s", step_names[step]);
    }
}

void mnode_print_ball(
    FILE * f,
    const struct node * const mnode)
{
    fprintf(f, " ball=%d", mnode->ball);
}

void pnode_print_path(
    FILE * f,
    const struct node * const pnode)
{
    const int qsteps = pnode->opts.qsteps;
    enum step path[qsteps];
    unpack_serie(pnode, path);
    for (int i = 0; i < qsteps; ++i) {
        fprintf(f, " %s", step_names[path[i]]);
    }
}

void snapshot_item(const struct mcts_ai * const me, const struct node * const node, int depth)
{
    if (node == NULL || node == me->nodes) {
        return;
    }

    FILE * f = get_flog();
    if (f == NULL) {
        return;
    }

    const int inode = node - me->nodes;

    const int type = node->opts.type;
    fprintf(f, "%*snode-%s #%d: ", 2*depth, "", node_types[type], inode);
    fprintf(f, "score=%d qgames=%d", node->score, node->qgames);

    switch (type) {
        case NODE_S:
            snode_print_steps(node);
            break;
        case NODE_M:
            mnode_print_ball(f, node);
            break;
        case NODE_P:
            pnode_print_path(f, node);
            break;
        case NODE_T:
            break;
    }

    fprintf(f, "\n");

    const int qanswers = node->opts.qanswers;
    for (int i = 0; i < qanswers; ++i) {
        const struct node * child = get_answer(me, node, i);
        if (child != NULL && child != me->nodes) {
            snapshot_item(me, child, depth + 1);
        }
    }

    fflush(f);
}

void mcts_log_snapshot(const struct mcts_ai * const me)
{
    FILE * f = get_flog();
    if (f == NULL) {
        return;
    }

    const struct node * const root = me->nodes + 1;
    snapshot_item(me, root, 0);
}

void mcts_log_state(const char * title, const struct state * const state)
{
    FILE * f = get_flog();
    if (f == NULL) {
        return;
    }

    fprintf(f, "State <%s>: active=%d ball=%d", title, state->active, state->ball);

    if (state->step1 != INVALID_STEP) {
        fprintf(f, " step1=%s", step_names[state->step1]);
    }

    if (state->step2 != INVALID_STEP) {
        fprintf(f, " step2=%s", step_names[state->step2]);
    }

    if (state->step12 != 0) {
        fprintf(f, " step12=%016llX", state->step12);
    }

    fprintf(f, "\n");
    fflush(f);
}

#endif



#ifdef MAKE_CHECK

#include "insider.h"

#define BW   15
#define BH   23
#define GW    4
#define FK    5

#define QROLLOUTS           1024
#define MIN_QTHINK    (32 * 1024)

static struct node * must_alloc_node(
    struct mcts_ai * restrict const me,
    enum node_type type)
{
    struct node * result = alloc_node(me, type, INVALID_STEP);
    if (result == NULL) {
        test_fail("alloc_node failed.");
    }

    return result;
}

int test_rollout(void)
{
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
        reset_cache(me);
        for (unsigned int i=0; i<ALLOCATED_NODES; ++i) {
            must_alloc_node(me, NODE_S);

            if (me->good_node_alloc != i+1) {
                test_fail("good_node_alloc mismatch, actual %u, expected %u.", me->good_node_alloc, i+1);
            }

            if (me->bad_node_alloc != 0) {
                test_fail("bad_node_alloc mismatch, actual %u, expected %u.", me->bad_node_alloc, 0);
            }
        }

        for (unsigned int i=0; i<ALLOCATED_NODES/2; ++i) {
            const struct node * node = alloc_node(me, NODE_S, INVALID_STEP);
            if (node != NULL) {
                test_fail("allocation failure expected");
            }

            if (me->good_node_alloc != ALLOCATED_NODES) {
                test_fail("good_node_alloc mismatch, actual %u, expected %u.", me->good_node_alloc, ALLOCATED_NODES);
            }

            if (me->bad_node_alloc != i+1) {
                test_fail("bad_node_alloc mismatch, actual %u, expected %u.", me->bad_node_alloc, i+1);
            }
        }

        if (j == 1) {
            init_cache(me, cache);
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
    reset_cache(me);

    const struct node * nodes[HISTORY_QITEMS];

    for (int i=0; i<HISTORY_QITEMS; ++i) {
        struct node * restrict const node = must_alloc_node(me, NODE_S);
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
    const uint32_t cache = 1024 * sizeof(struct node);

    must_init_ctx(&protocol_empty);
    struct ai * restrict const ai = ctx->ai;
    struct mcts_ai * restrict const me = ctx->mcts;

    must_set_param(ai, "cache", &cache);

    reset_cache(me);
    struct node * restrict const root = must_alloc_node(me, NODE_S);
    root->qgames = 1;

    me->C = 1.4;

    const int qanswers = 4;
    const struct { int qgames; int score; } stats[qanswers] = {
        { 3, 1 }, /* NORTH - weight 1.55985508 */
        { 4, 2 }, /* EAST  - weight 1.56219899 BEST */
        { 5, 3 }, /* SOUTH - weight 1.55005966 */
        { 6, 4 }, /* WEST  - weight 1.53394851 */
    };

    struct node * restrict const node = must_alloc_node(me, NODE_S);
    node->opts.qanswers = qanswers;
    node->qgames = 10;
    node->score = 0;

    for (int i=0; i<qanswers; ++i) {
        struct node * answer = must_alloc_node(me, NODE_S);
        int ianswer = answer - me->nodes;
        node->children[i] = ianswer;
        answer->qgames = stats[i].qgames;
        answer->score = stats[i].score;
    }

    const int answer = select_answer(me, node, qanswers);

    if (answer != 1) {
        test_fail("Unexpected answer %d, expected 1 (EAST).", answer);
    }

    root->opts.qanswers = QSTEPS;
    for (enum step step=0; step<QSTEPS; ++step) {
        struct node * restrict const child = must_alloc_node(me, NODE_S);
        child->qgames = 1;
        child->score = 2;
        root->children[step] = child - me->nodes;
    }

    steps_t visited = 0;
    for (int i=0; i<QSTEPS; ++i) {
        const int chosen = select_answer(me, root, QSTEPS);
        visited |= 1 << chosen;
        struct node * restrict const child = me->nodes + root->children[chosen];
        child->qgames = 1;
        child->score = (rand() % 3) - 1;
        ++root->qgames;
    }

    if (visited != 0xFF) {
        test_fail("Some directions are visitied twice, visited mask is 0x%02X.", visited);
    }

    free_ctx();
    return 0;
}

static int run_simulation(const struct game_protocol * const protocol, int qsimulations)
{
    const uint32_t cache = 128 * qsimulations * sizeof(struct node);

    const enum step * const steps = protocol->steps;
    const int qsteps = protocol->qsteps;

    must_init_ctx(protocol);
    struct ai * restrict const ai = ctx->ai;
    struct mcts_ai * restrict const me = ctx->mcts;

    must_set_param(ai, "cache", &cache);

    int status = ai->do_steps(ai, qsteps, steps);
    if (status != 0) {
        test_fail("Failed to apply moves, status %d, error: %s", status, ai->error);
    }

    reset_cache(me);

    struct node * restrict const zero = must_alloc_node(me, NODE_T);
    zero->score = 2;
    zero->qgames = 1;

    struct node * restrict const root = must_alloc_node(me, NODE_T);

    root->qgames = 1;
    for (int i=0; i<qsimulations; ++i) {
        log_line("\nSimulation %d", i);
        simulate(me, root);
        ++root->qgames;
    }

    if (root->qgames != qsimulations + 1) {
        test_fail("root->qgames = %u, but %u expected.", root->qgames, qsimulations);
    }

    free_ctx();
    return 0;
}

int test_simulation(void)
{
    return run_simulation(&protocol_empty, 1000);
}

int test_mcts_ai_unstep(void)
{
    const uint32_t qthink = MIN_QTHINK;

    must_init_ctx(&protocol_empty);
    struct ai * restrict const ai = ctx->ai;
    const struct geometry * const geometry = ctx->geometry;

    must_set_param(ai, "qthink", &qthink);

    unsigned int qsteps = 0;
    const struct state * const state = ai->get_state(ai);
    while (state_status(state) == IN_PROGRESS) {
        const enum step step = ai->go(ai, NULL);

        if (step < 0 || step >= INVALID_STEP) {
            test_fail("ai->go returns invalid step %d\n", step);
        }

        const struct warn * warn = ai->get_warn(ai, 0);
        if (warn != NULL) {
            info("ai->go returns %s", step_names[step]);
            test_fail("Warning after ai->go() at step %u: %s (at %s:%d)",
                qsteps, warn->msg, warn->file_name, warn->line_num);
        }

        int old_active = state->active;
        const int status = ai->do_step(ai, step);
        if (status != 0) {
            test_fail("ai->go step %s (%d) is not accepted by ai->do_step, qsteps = %d\n", step_names[step], step, qsteps);
        }

        int new_active = state->active;
        info("do_step %s, active %d -> %d", step_names[step], old_active, new_active);
        ++qsteps;
    }

    const int status = ai->undo_steps(ai, qsteps);
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

    destroy_state(check_state);
    free_ctx();
    return 0;
}

int run_ai_go(const struct game_protocol * const protocol, const int qmoves)
{
    const uint32_t qthink = MIN_QTHINK;

    const enum step * const steps = protocol->steps;
    const int qsteps = protocol->qsteps;

    must_init_ctx(protocol);
    struct ai * restrict const ai = ctx->ai;

    must_set_param(ai, "qthink", &qthink);

    int status = ai->do_steps(ai, qsteps, steps);
    if (status != 0) {
        test_fail("Failed to apply moves, status %d, error: %s", status, ai->error);
    }

    const struct state * const state = ai->get_state(ai);

    for (int i = 0; i < qmoves; ++i) {
        if (state_status(state) != IN_PROGRESS) {
            break;
        }

        enum step step = ai->go(ai, NULL);
        if (step < 0 || step >= INVALID_STEP) {
            test_fail("ai->go returns invalid step %d at move %d", step, i);
        }

        printf("ai->go returns %s\n", step_names[step]);
        const struct warn * warn = ai->get_warn(ai, 0);
        if (warn != NULL) {
            test_fail("Warning after ai->go() at move %d: %s (at %s:%d)",
                i, warn->msg, warn->file_name, warn->line_num);
        }

        status = ai->do_step(ai, step);
        if (status != 0) {
            test_fail("ai->do_step(%s) failed at move %d, status %d, error: %s",
                step_names[step], i, status, ai->error);
        }

        info("Move %d: %s", i, step_names[step]);
    }

    free_ctx();
    return 0;
}

int debug_ai_go(void)
{
    return run_ai_go(&protocol_empty, 0);
}

int debug_simulate(void)
{
    return run_simulation(&protocol_empty, 0);
}

#endif
