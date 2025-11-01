#include "paper-football.h"

enum cycle_result cycle_guard_push(struct cycle_guard * restrict me, int from, int to)
{
    if (me->qkicks >= me->capacity) {
        return CYCLE_FOUND;
    }

    int override = 0;
    int i = me->qkicks;
    while (i --> 0) {
        int count = 0
            + (from == me->kicks[i].from) + (from == me->kicks[i].to)
            + (to == me->kicks[i].from) + (to == me->kicks[i].to)
            ;
        if (count >= 2) {
            override = 1;
            break;
        }
    }

    if (override && me->qkicks >= 2) {
        i = me->qkicks;
        while (i --> 0) {
            if (me->kicks[i].to == to) {
                return CYCLE_FOUND;
            }
            if (!me->kicks[i].override) {
                break;
            }
        }
    }

    me->kicks[me->qkicks].from = from;
    me->kicks[me->qkicks].to = to;
    me->kicks[me->qkicks].override = override;
    me->qkicks++;

    return NO_CYCLE;
}



struct bsf_node
{
    struct dlist link;
    struct bsf_node * parent;
    struct state * state;
    struct cycle_guard * guard;
    enum step step;
    int depth;
};

static struct bsf_node * bsf_node(struct dlist * item)
{
    return ptr_move(item, -offsetof(struct bsf_node, link));
}

static struct bsf_node * bsf_alloc(
    struct bsf_free_kicks * restrict const me)
{
    if (is_dlist_empty(&me->free)) {
        return NULL;
    }

    struct dlist * first = me->free.next;
    dlist_remove(first);
    return bsf_node(first);
}

static void bsf_dealloc(
    struct bsf_free_kicks * restrict const me,
    struct bsf_node * restrict const node)
{
    dlist_insert_before(&node->link, &me->free);
}

static enum add_serie_status add_serie(
    struct warns * const warns,
    struct bsf_free_kicks * restrict const me,
    struct bsf_node * restrict node,
    int active,
    enum step step,
    int ball)
{
    int * restrict const ball_alts = ball >= 0 ? me->alts + ball : NULL;
    if (ball_alts != NULL && *ball_alts >= me->max_alts) {
        return ADDED_OK;
    }

    const int active1 = active == 1;
    const int goal1 = ball == GOAL_1;
    const int active2 = active == 2;
    const int goal2 = ball == GOAL_2;

    int win = (active1 && goal1) || (active2 && goal2);
    int loose = (active1 && goal2) || (active2 && goal1);

    struct bsf_serie * restrict serie = NULL;

    if (win) {
        if (me->win != NULL) {
            return ADDED_OK;
        }
        serie = me->series + me->capacity + 1;
    }

    if (loose) {
        if (me->loose != NULL) {
            return ADDED_OK;
        }
        serie = me->series + me->capacity;
    }

    if (serie == NULL) {
        serie = me->series + me->qseries;
    }

    int depth = node->depth;

    serie->ball = ball;
    serie->qsteps = depth + 1;
    serie->steps[depth] = step;

    while (depth > 0) {
        serie->steps[--depth] = node->step;
        node = node->parent;

        if (node == NULL) {
            WARN(warns, BSF_NODE_PARENT_NULL, "depth", depth, "qsteps", serie->qsteps);
            return ADDED_FAILURE;
        }
    };

    if (node != me->root) {
        WARN(warns, BSF_NODE_NOT_FROM_ROOT, "node", node, "root", me->root);
    }

    if (win) {
        me->win = serie;
    } else if (loose) {
        me->loose = serie;
    } else {
        ++me->qseries;
    }

    if (ball_alts != NULL) {
        ++ *ball_alts;
    }
    return me->qseries >= me->capacity ? ADDED_LAST : ADDED_OK;
}

static void bsf_go(
    struct warns * const warns,
    struct bsf_free_kicks * const me)
{
    const int max_depth = me->max_depth;
    const int max_visits = me->max_visits;

    struct dlist * restrict const free = &me->free;
    struct dlist * restrict const waiting = &me->waiting;
    struct dlist * restrict const used = &me->used;

    while (!is_dlist_empty(waiting)) {
        struct dlist * first = waiting->next;
        dlist_remove(first);

        struct bsf_node * restrict const parent = bsf_node(first);
        struct state * restrict const prev = parent->state;
        const int prev_ball = prev->ball;
        const int depth = parent->depth;

        const int qvisits = ++me->visits[prev_ball];
        if (qvisits >= max_visits) {
            dlist_insert_after(first, free);
            continue;
        }

        dlist_insert_after(first, used);

        steps_t steps = state_get_steps(prev);
        while (steps) {
            enum step step = extract_step(&steps);

            struct bsf_node * restrict const child = bsf_alloc(me);
            if (child == NULL) {
                WARN(warns, BSF_ALLOC_FAILED, "depth", depth, "capacity", me->capacity);
                return;
            }

            struct state * restrict const next = child->state;
            state_copy(next, prev);
            int next_ball = state_step(next, step);

            if (next_ball < 0 || !is_free_kick_situation(next)) {
                enum add_serie_status status = add_serie(warns, me, parent, prev->active, step, next_ball);
                bsf_dealloc(me, child);

                if (me->win) {
                    /* Not interested more */
                    return;
                }

                switch (status) {
                    case ADDED_LAST:
                        WARN(warns, BSF_SERIES_OVERFLOW, "qseries", me->qseries, "capacity", me->capacity);
                        return;
                    case ADDED_OK:
                    case ADDED_FAILURE:
                        continue;
                }
            }

            if (depth + 1 >= max_depth) {
                bsf_dealloc(me, child);
                continue;
            }

            struct cycle_guard * restrict const guard = parent->guard;
            enum cycle_result status = cycle_guard_push(guard, prev_ball, next_ball);
            if (status == CYCLE_FOUND) {
                bsf_dealloc(me, child);
                continue;
            }

            cycle_guard_copy(child->guard, guard);
            cycle_guard_pop(guard);

            child->step = step;
            child->parent = parent;
            child->depth = depth + 1;
            dlist_insert_before(&child->link, waiting);
        }
    }
}

struct bsf_free_kicks * create_bsf_free_kicks(
    const struct geometry * const geometry,
    int capacity,
    int max_depth,
    int max_alts,
    int max_visits)
{
    const uint32_t qpoints = geometry->qpoints;
    const uint32_t free_kick_len = geometry->free_kick_len;
    const uint32_t free_kick_reduce = (free_kick_len - 1) * (free_kick_len - 1);
    const size_t guard_capacity = 4 + qpoints / free_kick_reduce;

    const int max_depth_aligned = (max_depth + 7) & ~7;
    const int stats_sz = qpoints * sizeof(int);
    const size_t sizes[10] = {
        sizeof(struct bsf_free_kicks),
        capacity * sizeof(struct bsf_serie),
        capacity * max_depth_aligned * sizeof(enum step),
        capacity * sizeof(struct bsf_node),
        capacity * sizeof(struct state),
        capacity * qpoints,
        capacity * sizeof(struct cycle_guard),
        capacity * guard_capacity * sizeof(struct kick),
        stats_sz, stats_sz,
    };

    void * ptrs[10];
    void * data = multialloc(10, sizes, ptrs, 64);

    if (data == NULL) {
        return NULL;
    }

    struct bsf_free_kicks * restrict const me = data;
    struct bsf_serie * restrict const series = ptrs[1];
    enum step * restrict const steps_base = ptrs[2];
    struct bsf_node * restrict const nodes = ptrs[3];
    struct state * restrict const states = ptrs[4];
    uint8_t * restrict const lines_base = ptrs[5];
    struct cycle_guard * restrict const guards = ptrs[6];
    struct kick * restrict const kicks_base = ptrs[7];
    int * restrict const alts = ptrs[8];
    int * restrict const visits = ptrs[9];

    me->qseries = 0;
    me->capacity = capacity - 2;
    me->max_depth = max_depth;
    me->max_alts = max_alts;
    me->max_visits = max_visits;
    me->stats_sz = stats_sz;

    me->root = NULL;
    me->series = series;
    me->win = NULL;
    me->loose = NULL;
    me->alts = alts;
    me->visits = visits;
    me->states = states;

    dlist_init(&me->free);
    dlist_init(&me->waiting);
    dlist_init(&me->used);

    for (int i = 0; i < capacity; ++i) {
        series[i].steps = steps_base + i * max_depth_aligned;
    }

    for (int i = 0; i < capacity; ++i) {
        struct bsf_node * restrict const node = nodes + i;
        struct state * restrict const state = states + i;
        uint8_t * restrict const lines = lines_base + i * qpoints;
        struct cycle_guard * restrict const guard = guards + i;
        struct kick * restrict const kicks = kicks_base + i * guard_capacity;

        init_state(state, geometry, lines);
        node->state = state;

        guard->capacity = guard_capacity;
        guard->kicks = kicks;
        node->guard = guard;

        dlist_insert_before(&node->link, &me->free);
    }

    return me;
}

void destroy_bsf_free_kicks(struct bsf_free_kicks * restrict const me)
{
    if (me == NULL) {
        return;
    }

    const int capacity = me->capacity + 2;
    for (int i = 0; i < capacity; ++i) {
        free_state(&me->states[i]);
    }

    free(me);
}

void bsf_gen(
    struct warns * const warns,
    struct bsf_free_kicks * const me,
    const struct state * const state,
    const struct cycle_guard * const guard)
{
    struct dlist * restrict const free = &me->free;
    struct dlist * restrict const waiting = &me->waiting;
    struct dlist * restrict const used = &me->used;

    dlist_move_all(free, waiting);
    dlist_move_all(free, used);
    me->qseries = 0;

    struct bsf_node * root = bsf_alloc(me);
    if (root == NULL) {
        WARN(warns, BSF_ALLOC_FAILED, "depth", 0, "capacity", me->capacity);
        return;
    }

    dlist_insert_after(&root->link, waiting);

    root->parent = NULL;
    root->step = INVALID_STEP;
    root->depth = 0;
    state_copy(root->state, state);
    cycle_guard_reset(root->guard);
    me->root = root;
    me->win = NULL;
    me->loose = NULL;

    memset(me->alts, 0, me->stats_sz);
    memset(me->visits, 0, me->stats_sz);
    bsf_go(warns, me);
}



#ifdef MAKE_CHECK

#include "insider.h"

static void run_cycle_test(struct cycle_guard * restrict guard, const int * const path, int count) {
    cycle_guard_reset(guard);

    int expected_cycle_at = count - 1;
    int cycle_found_at = -1;

    for (int i = 1; i < count; ++i) {
        int from = path[i-1];
        int to = path[i];
        enum cycle_result result = cycle_guard_push(guard, from, to);
        if (result == CYCLE_FOUND) {
            cycle_found_at = i;
            break;
        }
    }

    if (cycle_found_at != expected_cycle_at) {
        test_fail("Expected cycle at step %d, got %d", expected_cycle_at, cycle_found_at);
    }
}

int test_cycle_detection(void)
{
    const size_t capacity = 100;
    struct kick * kicks = malloc(capacity * sizeof(struct kick));
    if (kicks == NULL) {
        test_fail("Failed to allocate kicks array");
    }

    struct cycle_guard guard;
    guard.capacity = capacity;
    guard.kicks = kicks;

    int test1[] = { 1, 2, 1, 2 };
    int test2[] = { 1, 2, 3, 1, 2, 1 };
    int test3[] = { 1, 2, 3, 1, 2, 3, 1 };
    int test4[] = { 1, 2, 1, 3, 1, 2, 1 };
    int test5[] = { 1, 2, 3, 2, 4, 2, 1, 2 };
    int test6[] = { 1, 2, 3, 4, 5, 6, 7, 5, 6, 7, 6 };

    run_cycle_test(&guard, test1, ARRAY_LEN(test1));
    run_cycle_test(&guard, test2, ARRAY_LEN(test2));
    run_cycle_test(&guard, test3, ARRAY_LEN(test3));
    run_cycle_test(&guard, test4, ARRAY_LEN(test4));
    run_cycle_test(&guard, test5, ARRAY_LEN(test5));
    run_cycle_test(&guard, test6, ARRAY_LEN(test6));

    free(kicks);
    return 0;
}

static void check_prep_step(
    struct preparation * restrict const prep,
    const enum step expected)
{
    enum step peeked = preparation_peek(prep);
    if (peeked != expected) {
        test_fail("peek expected %d, got %d", expected, peeked);
    }

    enum step popped = preparation_pop(prep);
    if (popped != expected) {
        test_fail("pop expected %d, got %d", expected, popped);
    }
}

int test_preparation(void)
{
    const enum step steps[] = { NORTH_EAST, SOUTH_WEST, SOUTH_EAST, NORTH_WEST, NORTH };
    const int qsteps = ARRAY_LEN(steps);

    struct preparation prep = {
        .qpreps = qsteps,
        .current = 0
    };
    memcpy(prep.preps, steps, qsteps * sizeof(enum step));

    for (int i = 0; i < qsteps; ++i) {
        check_prep_step(&prep, steps[i]);
    }

    check_prep_step(&prep, INVALID_STEP);

    return 0;
}

struct bsf_free_kicks * run_bsf(const struct game_protocol * const protocol, int qsteps_back)
{
    struct warns warns_storage;
    struct cycle_guard guard_storage;
    struct state state_storage;

    struct warns * restrict const warns = &warns_storage;
    struct cycle_guard * restrict const guard = &guard_storage;
    struct state * restrict const state = &state_storage;

    const struct std_geom * g = &protocol->geom.std;
    struct geometry * restrict const geometry = create_std_geometry(g->width, g->height, g->goal_width, g->free_kick_len);
    if (geometry == NULL) {
        test_fail("create_std_geometry(%d, %d, %d, %d) fails, return value is NULL, errno is %d.", g->width, g->height, g->goal_width, g->free_kick_len, errno);
    }

    const uint32_t qpoints = geometry->qpoints;
    uint8_t * restrict const lines = malloc(qpoints);

    warns_init(warns);
    cycle_guard_reset(guard);
    init_state(state, geometry, lines);

    const int qsteps = protocol->qsteps - qsteps_back;
    for (int i=0; i<qsteps; ++i) {
        const enum step step = protocol->steps[i];
        const int ball = state_step(state, step);
        if (ball == NO_WAY) {
            test_fail("State rejected step %s: move=%d; ball=NO_WAY", step_names[step], i);
        }
        if (ball < 0) {
            test_fail("Game had been finished unexpectedly: step %s: move=%d; ball=%d;", step_names[step], i, ball);
        }
    }

    struct bsf_free_kicks * fks = create_bsf_free_kicks(geometry, 1 << QANSWERS_BITS, MAX_FREE_KICK_SERIE, 8, 8);
    if (fks == NULL) {
        test_fail("create_bsf_free_kicks failed");
    }

    bsf_gen(warns, fks, state, guard);

    // Check for warnings during generation
    const struct warn * warn = warns_get(warns, 0);
    if (warn != NULL) {
        test_fail("Warning after bsf_gen: %s (at %s:%d)",
            warn->msg, warn->file_name, warn->line_num);
    }

    // Validate all generated series by replaying them
    struct state * restrict const current = create_state(geometry);
    if (current == NULL) {
        test_fail("Failed to create test state for validation");
    }

    for (int i = 0; i < fks->qseries; ++i) {
        struct bsf_serie * serie = &fks->series[i];
        const int qsteps = serie->qsteps;

        if (qsteps <= 0 || qsteps > fks->max_depth) {
            test_fail("Serie %d has invalid qsteps: %d", i, qsteps);
        }

        state_copy(current, state);

        // All steps except last should stay in penalty situation
        for (int j = 0; j < qsteps - 1; ++j) {
            enum step step = serie->steps[j];
            if (step < 0 || step >= QSTEPS) {
                test_fail("Serie %d step %d is invalid: %d", i, j, step);
            }

            int ball = state_step(current, step);
            if (ball == NO_WAY) {
                test_fail("Serie %d step %d (%s) is blocked", i, j, step_names[step]);
            }

            if (!is_free_kick_situation(current)) {
                test_fail("Serie %d step %d exits penalty before end", i, j);
            }
        }

        // Last step should exit penalty
        enum step last_step = serie->steps[qsteps - 1];
        int ball = state_step(current, last_step);
        if (ball == NO_WAY) {
            test_fail("Serie %d last step (%s) is blocked", i, step_names[last_step]);
        }

        if (ball != serie->ball) {
            test_fail("Serie %d: final ball %d != expected %d", i, ball, serie->ball);
        }

        if (ball >= 0 && ball < geometry->qpoints && is_free_kick_situation(current)) {
            test_fail("Serie %d ends in penalty situation at ball=%d", i, ball);
        }

        if (is_free_kick_situation(current)) {
            test_fail("Serie %d still in penalty after all steps", i);
        }
    }

    destroy_state(current);
    free_state(state);
    free(lines);
    destroy_geometry(geometry);
    return fks;
}

int test_gen_complete_free_kicks(void)
{
    struct bsf_free_kicks * restrict const fks = run_bsf(&protocol_fastest_free_kick1, 0);
    if (fks->qseries != 8) {
        test_fail("bsf_gen returned %d series, expected 8", fks->qseries);
    }

    destroy_bsf_free_kicks(fks);
    return 0;
}

int test_gen_complete_free_kicks_win(void)
{
    // Short game ending with penalty and goal
    struct bsf_free_kicks * restrict const fks = run_bsf(&protocol_000461, 0);

    if (fks->win == NULL) {
        test_fail("Win path not detected in game 000461");
    }

    if (fks->win->ball != GOAL_1) {
        test_fail("Win path leads to wrong goal: %d (expected GOAL_1=%d)", fks->win->ball, GOAL_1);
    }

    if (fks->win->qsteps != 1) {
        test_fail("Win path has %d steps, expected 1", fks->win->qsteps);
    }

    destroy_bsf_free_kicks(fks);
    return 0;
}

int test_long_free_kick_to_win(void)
{
    // Last move sequence: NW N SE (regular move) + W SE SW SW (4-step penalty to GOAL_2)
    // We run BFS on position before last penalty (cut last 4 steps)
    struct bsf_free_kicks * restrict const fks = run_bsf(&protocol_000050, 4);

    if (fks->win == NULL) {
        test_fail("Win path not detected in game 000050");
    }

    if (fks->win->ball != GOAL_2) {
        test_fail("Win path leads to wrong goal: %d (expected GOAL_2=%d)", fks->win->ball, GOAL_2);
    }

    if (fks->win->qsteps > 4) {
        test_fail("Win path has %d steps, expected <= 4", fks->win->qsteps);
    }

    destroy_bsf_free_kicks(fks);
    return 0;
}

int test_long_free_kick_to_loose(void)
{
    // Last moves:
    //   1 NW NW S + NE W E W S N NE
    //   2 NW N SE + W SE SW SW
    // 14 steps before we have a free kick with loose
    struct bsf_free_kicks * restrict const fks = run_bsf(&protocol_000050, 14);

    if (fks->loose == NULL) {
        test_fail("Loose path not detected in game 000050");
    }

    destroy_bsf_free_kicks(fks);
    return 0;
}

int test_gen_complete_free_kicks_long(void)
{
    struct bsf_free_kicks * restrict const fks = run_bsf(&protocol_with_hang, 0);

    if (fks->qseries == 0) {
        test_fail("No series generated for real hung game penalty situation");
    }

    destroy_bsf_free_kicks(fks);
    return 0;
}

#endif
