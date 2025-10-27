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
int test_random_ai(void);
int test_rollout(void);
int test_node_cache(void);
int test_mcts_history(void);
int test_ucb_formula(void);
int test_simulation(void);
int test_random_ai_unstep(void);
int test_mcts_ai_unstep(void);
int test_cycle_detection(void);
int test_gen_complete_free_kicks(void);
int test_gen_complete_free_kicks_win(void);
int test_long_free_kick_to_win(void);
int test_long_free_kick_to_loose(void);
int test_gen_complete_free_kicks_long(void);
int test_preparation(void);

int debug_ai_go(void);
int debug_simulate(void);
