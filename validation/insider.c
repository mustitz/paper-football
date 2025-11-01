#include "insider.h"
#include "paper-football.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

const char * test_name = "";
int opt_verbose = 0;

void fail(void)
{
    fprintf(stderr, "\n");
    exit(1);
}

void test_fail(const char * const fmt, ...)
{
    fprintf(stderr, "Test `%s' fails: ", test_name);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fail();
}

void info(const char * const fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}



/* Tests */

int test_empty(void)
{
    return 0;
}



/* Run/list tests */

typedef int (* test_function)(void);

struct test_item
{
    const char * name;
    test_function function;
};

const struct test_item tests[] = {
    { "empty", &test_empty },
    { "multialloc", &test_multialloc },
    { "parser", &test_parser },
    { "std-geometry", &test_std_geometry },
    { "magic-step3", &test_magic_step3 },
    { "step", &test_step },
    { "step2", &test_step2 },
    { "history", &test_history },
    { "step12-overflow", &test_step12_overflow_error },
    { "geometry-straight-dist", &test_geometry_straight_dist},
    { "random-ai", &test_random_ai },
    { "rollout", &test_rollout },
    { "node-cache", &test_node_cache },
    { "mcts-history", &test_mcts_history },
    { "ucb-formula", &test_ucb_formula },
    { "simulation", &test_simulation },
    { "random-ai-unstep", &test_random_ai_unstep},
    { "mcts-ai-unstep", &test_mcts_ai_unstep},
    { "cycle-detection", &test_cycle_detection},
    { "preparation", &test_preparation},
    { "gen-complete-free-kicks", &test_gen_complete_free_kicks},
    { "gen-complete-free-kicks-win", &test_gen_complete_free_kicks_win},
    { "long-free-kick-to-win", &test_long_free_kick_to_win},
    { "long-free-kick-to-loose", &test_long_free_kick_to_loose},
    { "gen-complete-free-kicks-long", &test_gen_complete_free_kicks_long},

    { "debug-ai-go", &debug_ai_go},
    { "debug-simulate", &debug_simulate},
    { NULL, NULL }
};

void print_tests(void)
{
    const struct test_item * current = tests;
    for (; current->name != NULL; ++current) {
        printf("%s\n", current->name);
    }
}

void run_test_item(const struct test_item * const item)
{
    test_name = item->name;
    printf("Run test for %s:\n", item->name);
    const int test_exit_code = (*item->function)();
    if (test_exit_code) {
        exit(test_exit_code);
    }
}

void run_all_tests(void)
{
    const struct test_item * current = tests;
    for (; current->name != NULL; ++current) {
        run_test_item(current);
    }
}

void run_test(const char * const name)
{
    if (strcmp(name, "all") == 0) {
        return run_all_tests();
    }

    const struct test_item * current = tests;
    for (; current->name != NULL; ++current) {
        if (strcmp(name, current->name) == 0) {
            return run_test_item(current);
        }
    }

    fprintf(stderr, "Test “%s” is not found.", name);
    fail();
}

int main(const int argc, const char * const argv[])
{
    if (argc == 1) {
        print_tests();
        return 0;
    }

    for (size_t i=1; i<argc; ++i) {
        if (strcmp(argv[i], "-v") == 0) {
            opt_verbose = 1;
            continue;
        }
        run_test(argv[i]);
    }

    return 0;
}
