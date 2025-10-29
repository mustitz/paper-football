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

#endif
