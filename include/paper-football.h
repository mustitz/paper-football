#ifndef YOO__PAPER_FOOTBALL__H__
#define YOO__PAPER_FOOTBALL__H__

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_LEN(a) (sizeof(a)/(sizeof(a[0])))
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

static inline ptrdiff_t ptr_diff(const void * const a, const void * const b)
{
    const char * const byte_ptr_a = a;
    const char * const byte_ptr_b = b;
    return byte_ptr_b - byte_ptr_a;
}

static inline void * ptr_move(void * const ptr, const ptrdiff_t delta)
{
    char * const byte_ptr = ptr;
    return byte_ptr + delta;
}

void * multialloc(
    const size_t n,
    const size_t * const sizes,
    void * restrict * ptrs,
    const size_t granularity);

/*
 * Double linked lists.
 */

struct dlist
{
    struct dlist * next;
    struct dlist * prev;
};

static inline void dlist_init(struct dlist * restrict const me)
{
    me->next = me;
    me->prev = me;
}

static inline int is_dlist_empty(const struct dlist * const me)
{
    return me->next == me;
}

static inline void dlist_insert_after(
    struct dlist * const infant,
    struct dlist * const prev)
{
    struct dlist * next = prev->next;
    infant->next = next;
    infant->prev = prev;
    next->prev = infant;
    prev->next = infant;
};

static inline void dlist_insert_before(
    struct dlist * const infant,
    struct dlist * const next)
{
    struct dlist * prev = next->prev;
    infant->next = next;
    infant->prev = prev;
    prev->next = infant;
    next->prev = infant;
}

static inline void dlist_remove(
    struct dlist * const creaker)
{
    creaker->prev->next = creaker->next;
    creaker->next->prev = creaker->prev;
}

static inline void dlist_move_all(
    struct dlist * restrict const dst,
    struct dlist * restrict const src)
{
    if (is_dlist_empty(src)) {
        return;
    }

    struct dlist * src_first = src->next;
    struct dlist * src_last = src->prev;
    struct dlist * dst_last = dst->prev;

    dst_last->next = src_first;
    src_first->prev = dst_last;

    src_last->next = dst;
    dst->prev = src_last;

    dlist_init(src);
}



void debug_trap(void);


#define GOAL_1   -1
#define GOAL_2   -2
#define NO_WAY   -3

#define CACHE_AUTO_CALCULATE 0

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

extern const char * step_names[QSTEPS];

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

static inline int is_free_kick_situation(const struct state * const state)
{
    /* step1 == INVALID_STEP occurs in two cases:
     * 1. Penalty situation: step12 == 0 (cleared 1-2 steps are nonsence here)
     * 2. Start of first move: step12 != 0 (contains possble 1-2 step combinations) */
    return state->step1 == INVALID_STEP && state->step12 == 0;
}

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
    enum step step;
    int32_t qgames;
    double score;
};

struct cache_explanation
{
    uint32_t used;
    uint32_t total;
    uint32_t good_alloc;
    uint32_t bad_alloc;
};

struct ai_explanation
{
    size_t qstats;
    const struct step_stat * stats;
    double time;
    double score;
    struct cache_explanation cache;
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

struct warn {
    const char * msg;
    const char * param1;
    uint64_t value1;
    const char * param2;
    uint64_t value2;
    const char * file_name;
    int line_num;
    int num;
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
        struct ai_explanation * restrict const explanation);

    const struct ai_param * (*get_params)(const struct ai * const ai);

    int (*set_param)(
        struct ai * restrict const ai,
        const char * const name,
        const void * const value);

    const struct state * (*get_state)(const struct ai * const ai);

    void (*free)(struct ai * restrict const ai);

    const struct warn * (*get_warn)(
        struct ai * restrict const ai,
        int index);
};

int init_random_ai(
    struct ai * restrict const ai,
    const struct geometry * const geometry);

int init_mcts_ai(
    struct ai * restrict const ai,
    const struct geometry * const geometry);

#endif
