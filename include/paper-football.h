#ifndef YOO__PAPER_FOOTBALL__H__
#define YOO__PAPER_FOOTBALL__H__

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void * multialloc(
    const size_t n,
    const size_t * const sizes,
    void * restrict * ptrs,
    const size_t granularity);



#define GOAL_1   -1
#define GOAL_2   -2
#define NO_WAY   -3

enum step {
    NORTH_WEST = 0,
    NORTH,
    NORTH_EAST,
    EAST,
    SOUTH_EAST,
    SOUTH,
    SOUTH_WEST,
    WEST,
    QSTEPS
};

#define INVALID_STEP QSTEPS
#define ACTIVE_STEP_FREE_KICK  -1

#define CHANGE_PASS            -1
#define CHANGE_FREE_KICK       -2
#define CHANGE_STEP1           -3
#define CHANGE_STEP2           -4
#define CHANGE_STEP_12_LO      -5
#define CHANGE_STEP_12_HI      -6
#define CHANGE_ACTIVE          -7
#define CHANGE_BALL            -8

#define BACK(s) ((enum step)(((s)+4) & 0x07))

typedef uint32_t steps_t;

static inline int step_count(steps_t steps)
{
    return __builtin_popcount(steps);
}

static inline enum step first_step(steps_t steps)
{
    return __builtin_ctz(steps);
}

static inline enum step extract_step(steps_t * mask)
{
    enum step result = first_step(*mask);
    *mask &= *mask - 1;
    return result;
}



struct geometry
{
    uint32_t qpoints;
    uint32_t free_kick_len;
    const int32_t * connections;
    const int32_t * free_kicks;
};

struct geometry * create_std_geometry(
    const int width,
    const int height,
    const int goal_width,
    const int penalty_len);

void destroy_geometry(struct geometry * restrict const me);

struct state
{
    const struct geometry * geometry;
    uint8_t * lines;
    int active;
    int ball;
    enum step step1;
    enum step step2;
    uint64_t step12;
    struct step_change * step_changes;
    unsigned int qstep_changes;
    unsigned int step_changes_capacity;
};

enum state_status
{
    IN_PROGRESS = 0,
    WIN_1,
    WIN_2
};

void init_state(
    struct state * restrict const me,
    const struct geometry * const geometry,
    uint8_t * const lines);
void free_state(struct state * restrict const me);

struct state * create_state(const struct geometry * const geometry);
void destroy_state(struct state * restrict const me);

int state_copy(
    struct state * restrict const dest,
    const struct state * const str);

enum state_status state_status(const struct state * const me);
steps_t state_get_steps(const struct state * const me);
int state_step(struct state * restrict const me, const enum step step);
int state_rollback(
    struct state * restrict const me,
    const struct step_change * const changes,
    unsigned int qchanges);



struct step_change
{
    int what;
    uint32_t data;
};

struct history
{
    unsigned int qstep_changes;
    unsigned int capacity;
    struct step_change * step_changes;
};

void init_history(struct history * restrict const me);
void free_history(struct history * restrict const me);
int history_push(struct history * restrict const me, const struct state * const state);



struct step_stat
{
    int possible;
    int32_t qgames;
    int32_t score;
};

enum param_type
{
    NO_TYPE=0,
    I32,
    U32,
    F32,
    QPARAM_TYPES
};

extern size_t param_sizes[QPARAM_TYPES];

struct ai_param
{
    const char * name;
    const void * value;
    enum param_type type;
    size_t offset;
};

struct ai
{
    void * data;
    const char * error;
    struct history history;

    int (*reset)(
        struct ai * restrict const ai,
        const struct geometry * const geometry);

    int (*do_step)(
        struct ai * restrict const ai,
        const enum step step);

    int (*do_steps)(
        struct ai * restrict const ai,
        const unsigned int qsteps,
        const enum step steps[]);

    int (*undo_step)(struct ai * restrict const ai);
    int (*undo_steps)(struct ai * restrict const ai, const unsigned int qsteps);

    enum step (*go)(
        struct ai * restrict const ai,
        struct step_stat * restrict const stats);

    const struct ai_param * (*get_params)(const struct ai * const ai);

    int (*set_param)(
        struct ai * restrict const ai,
        const char * const name,
        const void * const value);

    const struct state * (*get_state)(const struct ai * const ai);

    void (*free)(struct ai * restrict const ai);
};

int init_random_ai(
    struct ai * restrict const ai,
    const struct geometry * const geometry);

int init_mcts_ai(
    struct ai * restrict const ai,
    const struct geometry * const geometry);

#endif
