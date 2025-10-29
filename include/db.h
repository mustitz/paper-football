#ifndef VALIDATION_DB_H
#define VALIDATION_DB_H

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

static struct geometry * must_create_std_geometry(const struct std_geom * const params)
{
    const int width = params->width;
    const int height = params->height;
    const int goal_width = params->goal_width;
    const int free_kick_len = params->free_kick_len;

    struct geometry * restrict const result = create_std_geometry(width, height, goal_width, free_kick_len);
    if (result == NULL) {
        test_fail("create_std_geometry(%d, %d, %d, %d) fails, return value is NULL, errno is %d.",
            width, height, goal_width, free_kick_len, errno);
    }

    return result;
}

static struct geometry * must_create_protocol_geometry(const struct game_protocol * const protocol)
{
    enum geometry_type geometry = protocol->geometry;
    switch (geometry) {
        case STD_GEOMETRY:
            return must_create_std_geometry(&protocol->geom.std);
        default:
            test_fail("game_protocol %s contains wrong geometry type %d", protocol->name, geometry);
    }

    return NULL;
}

extern struct game_protocol protocol_empty;
extern struct game_protocol protocol_fastest_free_kick1;
extern struct game_protocol protocol_with_hang;
extern struct game_protocol protocol_000050;
extern struct game_protocol protocol_002255;
extern struct game_protocol protocol_000461;

#endif
