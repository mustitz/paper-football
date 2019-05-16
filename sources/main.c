#include "paper-football.h"
#include "parser.h"

#include <stdio.h>

#define KW_QUIT             1
#define KW_PING             2
#define KW_STATUS           3
#define KW_NEW              4
#define KW_STEP             5
#define KW_HISTORY          6

#define ITEM(name) { #name, KW_##name }
struct keyword_desc keywords[] = {
    { "exit", KW_QUIT },
    ITEM(QUIT),
    ITEM(PING),
    ITEM(STATUS),
    ITEM(NEW),
    ITEM(STEP),
    ITEM(HISTORY),
    { NULL, 0 }
};

const char * step_names[QSTEPS] = {
    "NW", "N", "NE", "E", "SE", "S", "SW", "W"
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

static int new_game(
    struct cmd_parser * restrict const me,
    const int width,
    const int height,
    const int goal_width)
{
    struct geometry * restrict const geometry = create_std_geometry(width, height, goal_width);
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

    destroy_game(me);

    me->width = width;
    me->height = height;
    me->goal_width = goal_width;
    me->geometry = geometry;
    me->state = state;
    me->backup = backup;

    me->history.qsteps = 0;
    return 0;
}

static void restore_backup(
    struct cmd_parser * restrict const me,
    const unsigned int history_qsteps)
{
    struct state * old_state = me->state;
    me->state = me->backup;
    me->backup = old_state;
    me->history.qsteps = history_qsteps;
}

static enum step find_step(const void * const id, size_t len)
{
    for (enum step step=0; step<QSTEPS; ++step) {
        const char * const step_name = step_names[step];
        if (strlen(step_name) == len) {
            if (strncasecmp(step_names[step], id, len) == 0) {
                return step;
            }
        }
    }
    return INVALID_STEP;
}



void free_cmd_parser(struct cmd_parser * restrict const me)
{
    if (me->tracker) {
        destroy_keyword_tracker(me->tracker);
    }

    destroy_game(me);

    free_history(&me->history);
}

int init_cmd_parser(struct cmd_parser * restrict const me)
{
    me->tracker = NULL;
    me->geometry = NULL;
    me->state = NULL;
    me->backup = NULL;

    me->tracker = create_keyword_tracker(keywords, KW_TRACKER__IGNORE_CASE);
    if (me->tracker == NULL) {
        free_cmd_parser(me);
        return ENOMEM;
    }

    const int status = new_game(me, 9, 11, 2);
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
    int width, height, goal_width;

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

    if (!parser_check_eol(lp)) {
        error(lp, "End of line expected (NEW command is completed), but someting was found.");
        return;
    }

    new_game(me, width, height, goal_width);
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
        const unsigned int history_qsteps = me->history.qsteps;
        do {
            int status = parser_read_id(lp);
            if (status != 0) {
                error(lp, "Step direction expected.");
                return restore_backup(me, history_qsteps);
            }

            enum step step = find_step(lp->lexem_start, lp->current - lp->lexem_start);
            if (step == QSTEPS) {
                error(lp, "Invalid step direction, only NW, N, NE, E, SE, S, SW are supported.");
                return restore_backup(me, history_qsteps);
            }

            const int ball = state_step(me->state, step);
            if (ball == NO_WAY) {
                error(lp, "Direction occupied.");
                return restore_backup(me, history_qsteps);
            }

            status = history_push(&me->history, step);
            if (status != 0) {
                error(lp, "history_push failed with code %d.", status);
                return restore_backup(me, history_qsteps);
            }

            parser_skip_spaces(lp);
        } while (!parser_check_eol(lp));
    }
}

void process_history(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    if (!parser_check_eol(lp)) {
        error(lp, "End of line expected (HISTORY command is parsed), but someting was found.");
        return;
    }

    if (me->history.qsteps == 0) {
        return;
    }

    const enum step * ptr = me->history.steps;
    const enum step * const end = ptr + me->history.qsteps;
    printf("%s", step_names[*ptr++]);
    while (ptr != end) {
        printf(" %s", step_names[*ptr++]);
    }
    printf("\n");
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
        default:
            error(lp, "Unexpected keyword at the begginning of the line.");
            break;
    }

    return 0;
}

int main()
{
    struct cmd_parser cmd_parser;
    init_cmd_parser(&cmd_parser);

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
