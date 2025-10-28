#include "paper-football.h"

static const char * messages[QWARNS] = {
    [WARN_WRONG_WARN] = "Wrong warning",
    [WARN_STEPS_ARE_CYCLES] = "All steps are cycles!",
    [WARN_ACTIVE_OOR] = "state->active value is out of range",
    [WARN_INCONSISTERN_STEPS_PRIORITY] = "Inconsistent values for steps/priories",
    [WARN_BSF_ALLOC_FAILED] = "BSF node allocation failed",
    [WARN_BSF_SERIES_OVERFLOW] = "BSF series capacity exceeded",
    [WARN_BSF_NODE_PARENT_NULL] = "BSF node parent is NULL before reaching root",
    [WARN_BSF_NODE_NOT_FROM_ROOT] = "BSF serie path does not start from root",
    [0] = "???"
};

void warns_init(struct warns * const ws)
{
    ws->qwarns = 0;
}

void warns_add(
    struct warns * restrict const ws,
    int num,
    const char * param1,
    uint64_t value1,
    const char * param2,
    uint64_t value2,
    const char * file_name,
    int line_num)
{
    if (num <= 0 || num >= QWARNS) {
        warns_add(ws, WARN_WRONG_WARN, "num", num, NULL, 0, file_name, line_num);
        return;
    }

    for (int i=0; i<ws->qwarns; ++i) {
        if (ws->warns[i].num == num) {
            /* Already have it */
            return;
        }
    }

    int i = ws->qwarns;
    if (i >= QWARNS) {
        /* Overflow */
        return;
    }

    struct warn * restrict const warn = ws->warns + i;
    warn->num = num;
    warn->msg = messages[num];
    warn->param1 = param1;
    warn->param2 = param2;
    warn->value1 = value1;
    warn->value2 = value2;
    warn->file_name = file_name;
    warn->line_num = line_num;
    ++ws->qwarns;
}

void warns_reset(struct warns * const ws)
{
    ws->qwarns = 0;
}

const struct warn * warns_get(const struct warns * const ws, int index)
{
    if (index < 0 || index >= ws->qwarns) {
        return NULL;
    }

    return ws->warns + index;
}

const struct warn * ai_get_warn(struct ai * restrict const ai, int index)
{
    return warns_get(&ai->warns, index);
}
