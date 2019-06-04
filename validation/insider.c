#include "insider.h"
#include "paper-football.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

const char * test_name = "";

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
    { "history", &test_history },
    { "random-ai", &test_random_ai },
    { "rollout", &test_rollout },
    { "node-cache", &test_node_cache },
    { "mcts-history", &test_mcts_history },
    { "ucb-formula", &test_ucb_formula },
    { "simulation", &test_simulation },
    { "random-ai-unstep", &test_random_ai_unstep},
    { "mcts-ai-unstep", &test_mcts_ai_unstep},
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
        run_test(argv[i]);
    }

    return 0;
}
