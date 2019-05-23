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

#define BACK(step) ((enum step)((step+4) & 0x07))

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
    const int32_t * connections;
};

struct geometry * create_std_geometry(
    const int width,
    const int height,
    const int goal_width);

void destroy_geometry(struct geometry * restrict const me);

struct state
{
    const struct geometry * geometry;
    uint8_t * lines;
    int active;
    int ball;
    int ball_before_goal;
};

enum state_status
{
    IN_PROGRESS = 0,
    WIN_1,
    WIN_2
};

void init_lines(
    const struct geometry * const geometry,
    uint8_t * restrict const lines);

struct state * create_state(const struct geometry * const geometry);
void destroy_state(struct state * restrict const me);

int state_copy(
    struct state * restrict const dest,
    const struct state * const str);

enum state_status state_status(const struct state * const me);
steps_t state_get_steps(const struct state * const me);
int state_step(struct state * restrict const me, const enum step step);
int state_unstep(struct state * restrict const me, const enum step step);



struct history
{
    unsigned int qsteps;
    unsigned int capacity;
    enum step * steps;
};

void init_history(struct history * restrict const me);
void free_history(struct history * restrict const me);
int history_push(struct history * restrict const me, const enum step step);



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
