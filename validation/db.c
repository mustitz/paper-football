#include "insider.h"

#define NW    NORTH_WEST
#define  N         NORTH
#define NE    NORTH_EAST
#define  E          EAST
#define SE    SOUTH_EAST
#define  S         SOUTH
#define SW    SOUTH_WEST
#define  W          WEST



struct game_protocol protocol_empty = {
    .name = "empty",
    .geometry = STD_GEOMETRY,
    .geom.std = { 15, 23, 6, 5 },
    .qsteps = 0,
    .steps = NULL,
};



static enum step fastest_free_kick1[] = {
    N, N, N,
    E, E, E,
    SW, SW, N, /* It is a free kick */
};

struct game_protocol protocol_fastest_free_kick1 = {
    .name = "fastest free kick for player 1",
    .geometry = STD_GEOMETRY,
    .geom.std = { 15, 23, 6, 5 },
    .qsteps = ARRAY_LEN(fastest_free_kick1),
    .steps = fastest_free_kick1,
};



static enum step fastest_free_kick2[] = {
    NW, NW, NE,
    SE, S, NW, /* It is a free kick */
};

struct game_protocol protocol_fastest_free_kick2 = {
    .name = "fastest free kick for player 2",
    .geometry = STD_GEOMETRY,
    .geom.std = { 15, 23, 6, 5 },
    .qsteps = ARRAY_LEN(fastest_free_kick2),
    .steps = fastest_free_kick2,
};



static enum step steps_step12_overflow_bug_example[] = {
    NE, NE, NW,
    SW, S, NE, S,
    NE, NE, W,
    NE, NW, S, S,
    NE, NE, W,
    NE, SE, SW,
    E, NE, NW,
    N, NW, S, S,
    NE, NE, NE,
    SE, SW, N,
    SW, S, E,
    SW, W, N,
    SW, NW, W,
    SW, SE, NE,
    SE, E, E,
    SW, W, NW,
    SW, NW, NW,
    SW, SE, N,
    SE, SW, NW,
    SW, SE, N,
    SE, E, N,
    SE, NE, W,
    NE, SE, E,
    SW, SW, N,
    SW, SE, NE,
    SE, SW, N,
    SW, SE, NE,
    NE, SE, SW, NW, S,
    NE /* Impossible move NE was generated here */
};

struct game_protocol protocol_step12_overflow_bug_example = {
    .name = "step12 overflow bug example",
    .geometry = STD_GEOMETRY,
    .geom.std = { 21, 31, 6, 5 },
    .qsteps = ARRAY_LEN(steps_step12_overflow_bug_example),
    .steps = steps_step12_overflow_bug_example,
};


static enum step game_with_hang_steps[] = {
    NE, NE, W,
    SW, SW, SE,
    W, NW, NE,
    W, SW, SE,
    W, NW, NE,
    W, SW, SE,
    W, NW, NE,
    NW, SW, S,
    NW, N, E,
    NE, E, S,
    E, E, NW,
    E, E, S,
    NE, SE, W, N,
    SE, SE, SW,
    E, S, W, N,
    SE, SE, W,
    SE, NE, NW,
    NE, SE, W,
    SE, NE, NW,
    E, SE, S,
    NE, N, W,
    NW, NE, S,
    NE, N, W,
    N, SW, SW,
    W, N, E,
    NW, SW, S,
    SW, E, E,
    SW, W, NW,
    NW, NE, S,
    NE, N, E,
    NE, NW, S,
    NW, SW, E,
    SW, SW, W,
    SW, SE, N,
    SE, SW, E,
    SW, W, SE,
    SW, S, E, N,
    SW, SW, SE,
    SW, SW, E, NE, E,
    E, SE, S,
    SE, SW, NW,
    SW, W, N,
    SW, SE, E,
    SE, SW, N,
    W, S, SE,
    SW, SE, W,
    SE, NE, NW,
    E, SE, SW,
    SW, NW, NW,
    SW, SE, N,
    SE, S, E,
    SE, S, NW,
    W, W, N,
    SW, SW, SE,
    E, NE, W, N,
    NW, NE, S,
    NE, NW, SW,
    SW, W, NE,
    NE, NE, N,
    SW, W, N, SE, SW,
    NW, E, N,
    N, E, S, S,
    NW, W, NE,
    W, SW, SE,
    W, NW, NE,
    NW, NE, SE, NE, S,
    S, S, SE,
    NE, SE, SE,
    W, SW, N,
    SW, SE, NE,
    SE, SW, SE,
    SW, NW, N,
    W, NW, E,
    NW, NE, NE, S,
    S, NW, S,
    NW, NW, E,
    NW, W, NE,
    NW, W, NW,
    SW, SW, N,
    SW, S, E,
    SW, NW, N,
    NW, SW, E,
    SW, NW, NE,
    W, SW, SE,
    W, NW, NE,
    NE, W, NE,
    NW, NW, E,
    E, S, SE,
    E, SE, S,
    NE, NW, N,
    N, W, W,
    N, W, NE,
    E, E, SW,
    E, NE, N,
    NW, SW, E, NE, NW, S, E, N, SW, NE, W, SE, W, S, N, E, NW, E, NW,
    NW, NW, NW,
    SW, W, NE, /* Hang now: SE, E, W, S, SE, NW, N, SE, W, N, E, S, S, N, W, N, E, SW, S, NE, S, W, E, NW, E, N, W, NW, S, */
};

struct game_protocol protocol_with_hang = {
    .name = "a debug game with_hang",
    .geometry = STD_GEOMETRY,
    .geom.std = { 21, 31, 6, 5 },
    .qsteps = ARRAY_LEN(game_with_hang_steps),
    .steps = game_with_hang_steps,
};



static enum step game_000050[] = {
    N, NE, NW,
    SW, S, NE, S,
    NE, NE, NW,
    E, SE, SW,
    E, NE, NW,
    E, SE, SW,
    E, NE, NW,
    E, SE, SW,
    S, S, E,
    SE, SE, SE,
    W, W, N,
    SW, SE, E,
    SW, E, NE, NW,
    SW, SE, N,
    SE, SW, W,
    SE, NE, E,
    SW, SW, NW,
    SW, S, NE,
    SE, W, SE,
    SW, S, S,
    S, E, S,
    NE, S, NE,
    S, NE, SE,
    N, NW, W,
    NW, NW, S, NE, W, E, W, S, N, NE,
    NW, N, SE, W, SE, SW, SW,
};

struct game_protocol protocol_000050 = {
    .name = "game 000050",
    .geometry = STD_GEOMETRY,
    .geom.std = { 21, 31, 6, 5 },
    .qsteps = ARRAY_LEN(game_000050),
    .steps = game_000050,
};



static enum step game_000461[] = {
    N, N, NE,
    S, E, S,
    NE, N, W,
    NE, NW, E,
    NW, NE, NW,
    NE, S, E,
    N, NW, NW,
    NE, SE, E,
    S, W, NW, /* Free kick wins here */
};

struct game_protocol protocol_000461 = {
    .name = "game 000461",
    .geometry = STD_GEOMETRY,
    .geom.std = { 21, 31, 6, 5 },
    .qsteps = ARRAY_LEN(game_000461),
    .steps = game_000461,
};



enum step steps_from_game_002255_loop_in_engine_answer[] = {
    N, NW, NE,
    SE, S, NW, S,
    NW, NW, E,
    NW, NE, S, S,
    NW, NW, NE,
    W, SW, SE,
    W, NW, NE,
    W, SW, SE,
    W, NW, NE,
    NW, W, SE,
    W, W, NW,
    NE, E, SW, S, /* Now engine move contained loops: NE NE NW E W E W E W E W E W E W E W E W E NE */
};

struct game_protocol protocol_002255 = {
    .name = "game 002255 with loop in engine answer",
    .geometry = STD_GEOMETRY,
    .geom.std = { 21, 31, 6, 5 },
    .qsteps = ARRAY_LEN(steps_from_game_002255_loop_in_engine_answer),
    .steps = steps_from_game_002255_loop_in_engine_answer,
};
