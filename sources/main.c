#include "hashes.h"
#include "paper-football.h"
#include "parser.h"

#include <stdio.h>

#define KW_QUIT             1
#define KW_PING             2
#define KW_STATUS           3
#define KW_NEW              4
#define KW_STEP             5
#define KW_HISTORY          6
#define KW_SET              7
#define KW_AI               8
#define KW_GO               9
#define KW_INFO            10
#define KW_TIME            11
#define KW_SCORE           12
#define KW_STEPS           13

#define ITEM(name) { #name, KW_##name }
struct keyword_desc keywords[] = {
    { "exit", KW_QUIT },
    ITEM(QUIT),
    ITEM(PING),
    ITEM(STATUS),
    ITEM(NEW),
    ITEM(STEP),
    ITEM(HISTORY),
    ITEM(SET),
    ITEM(AI),
    ITEM(GO),
    ITEM(INFO),
    ITEM(TIME),
    ITEM(SCORE),
    ITEM(STEPS),
    { NULL, 0 }
};

const char * step_names[QSTEPS] = {
    "NW", "N", "NE", "E", "SE", "S", "SW", "W"
};

enum ai_go_flags { EXPLAIN_TIME, EXPLAIN_SCORE, EXPLAIN_STEPS };

struct ai_desc
{
    const char * name;
    const char * sha512;
    int (*init_ai)(struct ai * restrict const ai, const struct geometry * const geometry);
};

struct ai_desc ai_list[] = {
    {   "mcts",   MCTS_AI_HASH,   &init_mcts_ai },
    { "random", RANDOM_AI_HASH, &init_random_ai },
    { NULL, NULL, NULL }
};

struct cmd_parser
{
    struct line_parser line_parser;
    const struct keyword_tracker * tracker;

    int width;
    int height;
    int goal_width;
    struct geometry * geometry;
    struct state * state;
    struct state * backup;

    struct history history;

    struct ai * ai;
    const struct ai_desc * ai_desc;
    struct ai ai_storage;
};



static void error(struct line_parser * restrict const lp, const char * fmt, ...) __attribute__ ((format (printf, 2, 3)));

static void error(struct line_parser * restrict const lp, const char * fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Parsing error: ");
    vfprintf(stderr, fmt, args);
    va_end(args);

    int offset = lp->lexem_start - lp->line;
    fprintf(stderr, "\n> %s> %*s^\n", lp->line, offset, "");
}

static int read_keyword(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    parser_skip_spaces(lp);
    return parser_read_keyword(lp, me->tracker);
}

static void destroy_game(const struct cmd_parser * const me)
{
    if (me->geometry) {
        destroy_geometry(me->geometry);
    }

    if (me->state) {
        destroy_state(me->state);
    }

    if (me->backup) {
        destroy_state(me->backup);
    }
}

static void free_ai(struct cmd_parser * restrict const me)
{
    if (me->ai) {
        me->ai->free(me->ai);
        me->ai = NULL;
        me->ai_desc = NULL;
    }
}

static int new_game(
    struct cmd_parser * restrict const me,
    const int width,
    const int height,
    const int goal_width,
    const int free_kick_len)
{
    struct geometry * restrict const geometry = create_std_geometry(width, height, goal_width, free_kick_len);
    if (geometry == NULL) {
        return errno;
    }

    struct state * restrict const state = create_state(geometry);
    if (state == NULL) {
        destroy_geometry(geometry);
        return ENOMEM;
    }

    struct state * restrict const backup = create_state(geometry);
    if (backup == NULL) {
        destroy_geometry(geometry);
        destroy_state(state);
        return ENOMEM;
    }

    if (me->ai) {
        const int status = me->ai->reset(me->ai, geometry);
        if (status != 0) {
            destroy_geometry(geometry);
            destroy_state(state);
            destroy_state(backup);
            return status;
        }
    }

    destroy_game(me);

    me->width = width;
    me->height = height;
    me->goal_width = goal_width;
    me->geometry = geometry;
    me->state = state;
    me->backup = backup;

    me->history.qstep_changes = 0;
    return 0;
}

static void restore_backup(
    struct cmd_parser * restrict const me,
    const unsigned int history_qsteps)
{
    struct state * old_state = me->state;
    me->state = me->backup;
    me->backup = old_state;
    me->history.qstep_changes = history_qsteps;
}

static int is_match(
    const char * const name,
    const void * const id,
    const size_t id_len)
{
    if (strncmp(name, id, id_len) != 0) {
        return 0;
    }
    return name[id_len] == '\0';
}

static enum step find_step(const void * const id, const size_t len)
{
    for (enum step step=0; step<QSTEPS; ++step) {
        const char * const step_name = step_names[step];
        if (is_match(step_name, id, len)) {
            return step;
        }
    }
    return INVALID_STEP;
}

static const struct ai_param * find_ai_param(
    const struct ai * const ai,
    const void  * const id,
    const size_t id_len)
{
    const struct ai_param * ptr = ai->get_params(ai);
    for (; ptr->name != NULL; ++ptr) {
        if (is_match(ptr->name, id, id_len)) {
            return ptr;
        }
    }

    return NULL;
}

static int read_value(
    struct line_parser * restrict const lp,
    void * const buf,
    const int type)
{
    const size_t value_sz = param_sizes[type];
    if (value_sz == 0) {
        error(lp, "Parameter cannot be set.");
        return EINVAL;
    }

    if (type == I32 || type == U32) {
        int value;
        const unsigned char * const lexem = lp->current;
        const int status = parser_read_last_int(lp, &value);
        if (status != 0) {
            error(lp, "Single integer parameter value expected.");
            return EINVAL;
        }

        if (type == I32) {
            *(int32_t*)buf = value;
        }

        if (type == U32) {
            if (value < 0) {
                lp->lexem_start = lexem;
                error(lp, "Parameter value might be positive.");
                return EINVAL;
            }
            *(uint32_t*)buf = (uint32_t)value;
        }
    }

    if (type == F32) {
        float value;
        const int status = parser_read_float(lp, &value);
        if (status != 0) {
            error(lp, "Single float parameter expected.");
            return EINVAL;
        }

        *(float*)buf = value;
    }

    return 0;
}

static void set_ai(
    struct cmd_parser * restrict const me,
    const struct ai_desc * const ai_desc)
{
    struct ai storage;

    const int status = ai_desc->init_ai(&storage, me->geometry);
    if (status != 0) {
        fprintf(stderr, "Cannot set AI: init failed with code %d.\n", status);
        return;
    }

    const struct step_change * step_change = me->history.step_changes;
    const struct step_change * const end = step_change + me->history.qstep_changes;
    for (; step_change != end; ++step_change) {
        const int what = step_change->what;
        if (what != CHANGE_PASS && what != CHANGE_FREE_KICK) {
            continue;
        }
        const enum step step = step_change->data;
        const int status = storage.do_step(&storage, step);
        if (status != 0) {
            fprintf(stderr, "Cannot set AI: cannot apply history, status = %d.\n", status);
            storage.free(&storage);
            return;
        }
    }

    if (me->ai) {
        me->ai->free(me->ai);
    }

    me->ai_storage = storage;
    me->ai = &me->ai_storage;
    me->ai_desc = ai_desc;
}

static struct ai * get_ai(struct cmd_parser * restrict const me)
{
    if (me->ai) {
        return me->ai;
    }

    set_ai(me, ai_list);
    return me->ai;
}

static void restore_ai(
    struct cmd_parser * restrict const me,
    const unsigned int history_qsteps)
{
    struct ai * restrict const ai = me->ai;
    restore_backup(me, history_qsteps);

    const int status = ai->reset(ai, me->geometry);
    if (status != 0) {
        fprintf(stderr, "Cannot reset AI, AI turned off.\n");
        free_ai(me);
        return;
    }

    const struct step_change * step_change = me->history.step_changes;
    const struct step_change * const end = step_change + me->history.qstep_changes;
    for (; step_change != end; ++step_change) {
        const int what = step_change->what;
        if (what != CHANGE_PASS && what != CHANGE_FREE_KICK) {
            continue;
        }
        const int status = ai->do_step(ai, step_change->data);
        if (status != 0) {
            fprintf(stderr, "Cannot apply history to AI, AI turned off.\n");
            free_ai(me);
            return;
        }
    }
}

static void explain_step(
    const enum step step,
    const unsigned int flags,
    const struct ai_explanation * const explanation)
{
    if (flags == 0) {
        return;
    }

    const unsigned int time_mask = 1 << EXPLAIN_TIME;
    const unsigned int score_mask = 1 << EXPLAIN_SCORE;
    const unsigned int step_mask = 1 << EXPLAIN_STEPS;

    const unsigned int line_mask = time_mask | score_mask;
    if (flags & line_mask) {
        printf("  %2s", step_names[step]);
        if (flags & time_mask) {
            printf(" in %.3fs", explanation->time);
        }
        if (flags & score_mask) {
            const double score = explanation->score;
            if (score >= 0.0 && score <= 1.0) {
                printf(" score %5.1f%%", 100.0 * score);
            } else {
                printf(" score N/A");
            }
        }
        printf("\n");
    }

    if (flags & step_mask) {
        const struct step_stat * ptr = explanation->stats;
        const struct step_stat * const end = ptr + explanation->qstats;
        for (; ptr != end; ++ptr) {
            printf("        %2s %5.1f%%", step_names[ptr->step], 100 * ptr->score);
            if (ptr->qgames > 0) {
                printf(" %6d\n", ptr->qgames);
            } else {
                printf("    N/A\n");
            }
        }
    }
}

static void ai_go(
    struct cmd_parser * restrict const me,
    const unsigned int flags)
{
    struct ai_explanation explanation;

    if (state_status(me->state) != IN_PROGRESS) {
        fprintf(stderr, "Game over, no moves possible.\n");
        return;
    }

    struct ai * restrict const ai = get_ai(me);
    struct state * restrict const state = me->state;
    const int active = state->active;

    enum step step = ai->go(ai, flags ? &explanation : NULL);
    if (step == INVALID_STEP) {
        fprintf(stderr, "AI move: invalid step.\n");
        return;
    }

    state_copy(me->backup, state);
    const unsigned int history_qstep_changes = me->history.qstep_changes;

    for (;;) {
        const int ball = state_step(state, step);
        if (ball == NO_WAY) {
            printf("\n");
            fprintf(stderr, "ai_go: game state cannot follow step %s.\n", step_names[step]);
            restore_ai(me, history_qstep_changes);
            return;
        }

        history_push(&me->history, state);
        explain_step(step, flags, &explanation);

        const int status = me->ai->do_step(me->ai, step);
        if (status != 0) {
            printf("\n");
            fprintf(stderr, "ai_go: AI cannot follow himself on step %s.\n", step_names[step]);
            restore_ai(me, history_qstep_changes);
            return;
        }

        const int is_done = 0
            || state_status(state) != IN_PROGRESS
            || state->active != active
        ;

        if (is_done) {
            break;
        }

        step = ai->go(ai, flags ? &explanation : NULL);
        if (step == INVALID_STEP) {
            printf("\n");
            fprintf(stderr, "AI move: invalid step.\n");
            restore_ai(me, history_qstep_changes);
            return;
        }
    }

    const struct step_change * step_change_ptr = me->history.step_changes + history_qstep_changes;
    const struct step_change * const end = me->history.step_changes + me->history.qstep_changes;
    const char * separator = "";
    for (; step_change_ptr != end; step_change_ptr++) {
        const int what = step_change_ptr->what;
        const enum step step = step_change_ptr->data;
        if (what == CHANGE_PASS || what == CHANGE_FREE_KICK) {
            printf("%s%s", separator, step_names[step]);
            separator = " ";
        }
    }
    printf("\n");
}

static void ai_info(struct cmd_parser * restrict const me)
{
    get_ai(me);
    if (me->ai_desc == NULL) {
        return;
    }

    printf("%12s\t%12s\n", "name", me->ai_desc->name);
    printf("%12s\t%12.12s\n", "hash", me->ai_desc->sha512);

    const struct ai_param * ptr = me->ai->get_params(me->ai);
    for (; ptr->name != NULL; ++ptr) {
        switch (ptr->type) {
            case I32:
                printf("%12s\t%12d\n", ptr->name, *(int32_t*)ptr->value);
                break;
            case U32:
                printf("%12s\t%12u\n", ptr->name, *(uint32_t*)ptr->value);
                break;
            case F32:
                printf("%12s\t%12f\n", ptr->name, *(float*)ptr->value);
                break;
            default:
                break;
        }
    }
}



void free_cmd_parser(struct cmd_parser * restrict const me)
{
    if (me->tracker) {
        destroy_keyword_tracker(me->tracker);
    }

    destroy_game(me);
    free_ai(me);

    free_history(&me->history);
}

int init_cmd_parser(struct cmd_parser * restrict const me)
{
    me->tracker = NULL;
    me->geometry = NULL;
    me->state = NULL;
    me->backup = NULL;
    me->ai = NULL;
    me->ai_desc = NULL;

    me->tracker = create_keyword_tracker(keywords, KW_TRACKER__IGNORE_CASE);
    if (me->tracker == NULL) {
        free_cmd_parser(me);
        return ENOMEM;
    }

    const int status = new_game(me, 15, 23, 4, 5);
    if (status != 0) {
        free_cmd_parser(me);
        return status;
    }

    init_history(&me->history);

    return 0;
}



int process_quit(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    if (!parser_check_eol(lp)) {
        error(lp, "End of line expected (QUIT command is parsed), but someting was found.");
        return 0;
    }
    return 1;
}

void process_status(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    if (!parser_check_eol(lp)) {
        error(lp, "End of line expected (STATUS command is parsed), but someting was found.");
        return;
    }

    const struct state * const state = me->state;
    const int ball = state->ball;
    const int active = state->active;

    printf("Board width:   %4d\n", me->width);
    printf("Board height:  %4d\n", me->height);
    printf("Goal width:    %4d\n", me->goal_width);
    printf("Active player: %4d\n", active);
    if (ball >= 0) {
        printf("Ball position: %4d, %d\n", ball % me->width, ball / me->width);
    }

    static const char * status_strs[3] = {
        [IN_PROGRESS] = "in progress",
        [WIN_1]       = "player 1 win",
        [WIN_2]       = "player 2 win",
    };
    printf("Status:           %s\n", status_strs[state_status(state)]);
}

void process_new(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;

    int status;
    int width, height, goal_width, free_kick_len;

    parser_skip_spaces(lp);
    status = parser_try_int(lp, &width);
    if (status != 0) {
        error(lp, "Board width integer constant expected in NEW command.");
        return;
    }

    if (width % 2 != 1) {
        error(lp, "Board width integer constant should be odd number.");
        return;
    }

    if (width <= 4) {
        error(lp, "Board width integer constant should be at least 5 or more.");
        return;
    }

    parser_skip_spaces(lp);
    status = parser_try_int(lp, &height);
    if (status != 0) {
        error(lp, "Board height integer constant expected in NEW command.");
        return;
    }

    if (height % 2 != 1) {
        error(lp, "Board height integer constant should be odd number.");
        return;
    }

    if (height <= 4) {
        error(lp, "Board height integer constant should be at least 5 or more.");
        return;
    }

    parser_skip_spaces(lp);
    status = parser_try_int(lp, &goal_width);
    if (status != 0) {
        error(lp, "Board goal width integer constant expected in NEW command.");
        return;
    }

    if (goal_width % 2 != 0) {
        error(lp, "Goal width integer constant should be even number.");
        return;
    }

    if (goal_width <= 1) {
        error(lp, "Goal height integer constant should be at least 2 or more.");
        return;
    }

    if (goal_width + 3 > width) {
        error(lp, "Goal height integer constant should be less than width-1 = %d.", width-1);
        return;
    }

    parser_skip_spaces(lp);
    status = parser_try_int(lp, &free_kick_len);
    if (status != 0) {
        error(lp, "Free kick len integer constant expected in NEW command.");
        return;
    }

    if (free_kick_len <= 3) {
        error(lp, "Free kick len should be at least 4 or more.");
        return;
    }

    if (free_kick_len >= width/2) {
        error(lp, "Free kick length should be less than width half = %d.", width/2);
        return;
    }

    if (free_kick_len >= height/2) {
        error(lp, "Free kick length should be less than height half = %d.", height/2);
        return;
    }

    if (!parser_check_eol(lp)) {
        error(lp, "End of line expected (NEW command is completed), but someting was found.");
        return;
    }

    new_game(me, width, height, goal_width, free_kick_len);
}

void process_step(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    parser_skip_spaces(lp);
    if (parser_check_eol(lp)) {
        steps_t steps = state_get_steps(me->state);
        if (steps > 0) {
            const enum step step = extract_step(&steps);
            printf("%s", step_names[step]);
            while (steps != 0) {
                const enum step step = extract_step(&steps);
                printf(" %s", step_names[step]);
            }
            printf("\n");
        }
    } else {
        state_copy(me->backup, me->state);
        const unsigned int history_qstep_changes = me->history.qstep_changes;
        do {
            int status = parser_read_id(lp);
            if (status != 0) {
                error(lp, "Step direction expected.");
                return restore_backup(me, history_qstep_changes);
            }

            enum step step = find_step(lp->lexem_start, lp->current - lp->lexem_start);
            if (step == QSTEPS) {
                error(lp, "Invalid step direction, only NW, N, NE, E, SE, S, SW are supported.");
                return restore_backup(me, history_qstep_changes);
            }

            const int ball = state_step(me->state, step);
            if (ball == NO_WAY) {
                error(lp, "Direction occupied.");
                return restore_backup(me, history_qstep_changes);
            }

            status = history_push(&me->history, me->state);
            if (status != 0) {
                error(lp, "history_push failed with code %d.", status);
                return restore_backup(me, history_qstep_changes);
            }

            parser_skip_spaces(lp);
        } while (!parser_check_eol(lp));

        if (me->ai) {
            const struct step_change * ptr = me->history.step_changes + history_qstep_changes;
            const struct step_change * const end = me->history.step_changes + me->history.qstep_changes;
            for (; ptr != end; ++ptr) {
                const int what = ptr->what;
                if (what != CHANGE_PASS && what != CHANGE_FREE_KICK) {
                    continue;
                }
                const int status = me->ai->do_step(me->ai, ptr->data);
                if (status != 0) {
                    error(lp, "AI applying step sequence failed with code %d.", status);
                    return restore_backup(me, history_qstep_changes);
                }
            }
        }
    }
}

void process_history(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    if (!parser_check_eol(lp)) {
        error(lp, "End of line expected (HISTORY command is parsed), but someting was found.");
        return;
    }

    const struct step_change * ptr = me->history.step_changes;
    const struct step_change * const end = ptr + me->history.qstep_changes;
    const char * delimeter = "";
    for (; ptr != end; ++ptr) {
        const int what = ptr->what;
        if (what != CHANGE_PASS && what != CHANGE_FREE_KICK) {
            continue;
        }
        printf("%s%s", delimeter, step_names[ptr->data]);
        delimeter = " ";
    }

    if (delimeter[0] == ' ') {
        printf("\n");
    }
}

void process_set_ai_param(struct cmd_parser * restrict const me)
{
    int status;
    struct line_parser * restrict const lp = &me->line_parser;
    parser_skip_spaces(lp);

    status = parser_read_id(lp);
    if (status != 0) {
        error(lp, "AI parameter name expected.");
        return;
    }

    const size_t id_len = lp->current - lp->lexem_start;
    struct ai * restrict const ai = get_ai(me);
    if (ai == NULL) {
        return;
    }

    const struct ai_param * const param = find_ai_param(ai, lp->lexem_start, id_len);
    if (param == NULL) {
        error(lp, "Param is not found.");
        return;
    }

    parser_skip_spaces(lp);
    if (*lp->current == '=') {
        ++lp->current;
        parser_skip_spaces(lp);
    }

    const size_t value_sz = param_sizes[param->type];
    char buf[value_sz];
    status = read_value(lp, buf, param->type);
    if (status != 0) {
        return;
    }

    status = ai->set_param(ai, param->name, buf);
    if (status != 0) {
        fprintf(stderr, "%s\n", ai->error);
    }
}

void process_set_ai(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    parser_skip_spaces(lp);

    if (parser_check_eol(lp)) {
        const struct ai_desc * restrict ptr = ai_list;
        for (; ptr->name; ++ptr) {
            printf("%s\n", ptr->name);
        }
        return;
    }

    if (*lp->current == '.') {
        ++lp->current;
        return process_set_ai_param(me);
    }

    const unsigned char * const ai_name = lp->current;
    const int status = parser_read_id(lp);
    if (status != 0) {
        error(lp, "Invalid AI name, valid identifier expected.");
        return;
    }
    const size_t len = lp->current - ai_name;

    if (!parser_check_eol(lp)) {
        error(lp, "End of line expected but something was found in SET AI command.");
        return;
    }

    const struct ai_desc * restrict ptr = ai_list;
    for (; ptr->name; ++ptr) {
        const int match = 1
            && strlen(ptr->name) == len
            && strncasecmp(ptr->name, (const char *)ai_name, len) == 0
        ;

        if (match) {
            return set_ai(me, ptr);
        }
    }

    error(lp, "AI not found.");
}

void process_set(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    const int keyword = read_keyword(me);

    if (keyword == -1) {
        error(lp, "Invalid lexem in SET command.");
        return;
    }

    switch (keyword) {
        case KW_AI:
            return process_set_ai(me);
    }

    error(lp, "Invalid option name in SET command.");
}

void process_ai_go(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;

    unsigned int flags = 0;
    while (!parser_check_eol(lp)) {
        const int keyword = read_keyword(me);
        if (keyword == -1) {
            error(lp, "Invalid lexem in AI GO command.");
            return;
        }

        switch (keyword) {
            case KW_TIME:
                flags |= 1 << EXPLAIN_TIME;
                break;
            case KW_SCORE:
                flags |= 1 << EXPLAIN_SCORE;
                break;
            case KW_STEPS:
                flags |= 1 << EXPLAIN_STEPS;
                break;
            default:
                error(lp, "Invalid explain flag in AI GO command.");
                return;
        }

        parser_skip_spaces(lp);
        if (lp->current[0] == '|' || lp->current[0] == ',') {
            ++lp->current;
            parser_skip_spaces(lp);
        }
    }

    ai_go(me, flags);
}

void process_ai_info(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    if (!parser_check_eol(lp)) {
        error(lp, "End of line expected (AI INFO command is parsed), but someting was found.");
        return;
    }

    ai_info(me);
}

void process_ai(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    const int keyword = read_keyword(me);

    if (keyword == -1) {
        error(lp, "Invalid lexem in AI command.");
        return;
    }

    switch (keyword) {
        case KW_GO:
            return process_ai_go(me);
        case KW_INFO:
            return process_ai_info(me);
    }

    error(lp, "Invalid action in AI command.");
}

int process_cmd(struct cmd_parser * restrict const me, const char * const line)
{
    struct line_parser * restrict const lp = &me->line_parser;
    parser_set_line(lp, line);

    if (parser_check_eol(lp)) {
        return 0;
    }

    const int keyword = read_keyword(me);
    if (keyword == -1) {
        error(lp, "Invalid lexem at the begginning of the line.");
        return 0;
    }

    if (keyword == 0) {
        error(lp, "Invalid keyword at the begginning of the line.");
        return 0;
    }

    if (keyword == KW_QUIT) {
        return process_quit(me);
    }

    switch (keyword) {
        case KW_PING:
            printf("pong%s", lp->current);
            fflush(stdout);
            fflush(stderr);
            break;
        case KW_STATUS:
            process_status(me);
            break;
        case KW_NEW:
            process_new(me);
            break;
        case KW_STEP:
            process_step(me);
            break;
        case KW_HISTORY:
            process_history(me);
            break;
        case KW_SET:
            process_set(me);
            break;
        case KW_AI:
            process_ai(me);
            break;
        default:
            error(lp, "Unexpected keyword at the begginning of the line.");
            break;
    }

    return 0;
}

int main()
{
    struct cmd_parser cmd_parser;
    const int status = init_cmd_parser(&cmd_parser);
    if (status != 0) {
        fprintf(stderr, "Fatal: cannot init command line parser, error code is %d.\n", status);
        return status;
    }

    char * line = 0;
    size_t len = 0;
    for (;; ) {
        const ssize_t has_read = getline(&line, &len, stdin);
        if (has_read == -1) {
            break;
        }

        const int is_quit = process_cmd(&cmd_parser, line);
        if (is_quit) {
            break;
        }
    }

    free_cmd_parser(&cmd_parser);

    if (line) {
        free(line);
    }

    return 0;
}
