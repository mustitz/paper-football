#ifndef VALIDATION_INSIDER_H
#define VALIDATION_INSIDER_H

#include "paper-football.h"



enum geometry_type {
    STD_GEOMETRY,
    QGEOMETRIES
};

struct std_geom {
    int width;
    int height;
    int goal_width;
    int free_kick_len;
};

union geom_params {
    struct std_geom std;
};

struct game_protocol {
    const char * name;
    enum geometry_type geometry;
    union geom_params geom;
    int qsteps;
    const enum step * steps;
};



struct mcts_ctx
{
    struct geometry * geometry;
    struct ai * ai;
    struct mcts_ai * mcts;

    struct ai ai_storage;
};

extern struct mcts_ctx * restrict const ctx;

void must_init_ctx(
    const struct game_protocol * const protocol);

void free_ctx(void);

struct geometry * must_create_std_geometry(
    const struct std_geom * const params);

struct geometry * must_create_protocol_geometry(
        const struct game_protocol * const protocol);

void must_set_param(
    struct ai * restrict const ai,
    const char * const name,
    const void * const ptr);



extern struct game_protocol protocol_empty;
extern struct game_protocol protocol_fastest_free_kick1;
extern struct game_protocol protocol_fastest_free_kick2;
extern struct game_protocol protocol_step12_overflow_bug_example;
extern struct game_protocol protocol_with_hang;
extern struct game_protocol protocol_000050;
extern struct game_protocol protocol_000461;
extern struct game_protocol protocol_002255;



void test_fail(const char * const fmt, ...) __attribute__ ((format (printf, 1, 2)));
void info(const char * const fmt, ...) __attribute__ ((format (printf, 1, 2)));



int test_multialloc(void);
int test_parser(void);
int test_std_geometry(void);
int test_magic_step3(void);
int test_step(void);
int test_step2(void);
int test_history(void);
int test_step12_overflow_error(void);
int test_geometry_straight_dist(void);
int test_random_ai(void);
int test_rollout(void);
int test_node_cache(void);
int test_mcts_history(void);
int test_ucb_formula(void);
int test_simulation(void);
int test_random_ai_unstep(void);
int test_mcts_ai_unstep(void);
int test_cycle_detection(void);
int test_preparation(void);
int test_gen_complete_free_kicks(void);
int test_gen_complete_free_kicks_win(void);
int test_long_free_kick_to_win(void);
int test_long_free_kick_to_loose(void);
int test_gen_complete_free_kicks_long(void);

int debug_ai_go(void);
int debug_simulate(void);

#endif
