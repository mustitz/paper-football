void test_fail(const char * const fmt, ...) __attribute__ ((format (printf, 1, 2)));

int test_multialloc(void);
int test_parser(void);
int test_std_geometry(void);
int test_hockey_geometry(void);
int test_step(void);
int test_history(void);
int test_random_ai(void);
int test_rollout(void);
int test_node_cache(void);
int test_mcts_history(void);
int test_ucb_formula(void);
int test_simulation(void);
int test_unstep(void);
int test_random_ai_unstep(void);
int test_mcts_ai_unstep(void);
