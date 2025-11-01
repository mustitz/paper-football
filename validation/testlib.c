#include "insider.h"

struct mcts_ctx mcts_ctx_storage = { 0 };
struct mcts_ctx * restrict const ctx = &mcts_ctx_storage;

void must_init_ctx(
    const struct game_protocol * const protocol)
{
    memset(ctx, 0, sizeof(struct mcts_ctx));

    struct geometry * restrict const geometry = must_create_protocol_geometry(protocol);
    struct ai * restrict const ai = &ctx->ai_storage;

    init_mcts_ai(ai, geometry);
    struct mcts_ai * restrict const mcts = ai->data;

    ctx->geometry = geometry;
    ctx->ai = ai;
    ctx->mcts = mcts;
}

void free_ctx(void)
{
    struct geometry * restrict const geometry = ctx->geometry;
    struct ai * restrict const ai = ctx->ai;

    ai->free(ai);
    destroy_geometry(geometry);
}



struct geometry * must_create_std_geometry(const struct std_geom * const params)
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

struct geometry * must_create_protocol_geometry(const struct game_protocol * const protocol)
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



void must_set_param(
    struct ai * restrict const ai,
    const char * const name,
    const void * const ptr)
{
    const int status = ai->set_param(ai, name, ptr);
    if (status != 0) {
        test_fail("ai->set_param(%s, %p) fails with code %d, %s.", name, ptr, status, ai->error);
    }
}
