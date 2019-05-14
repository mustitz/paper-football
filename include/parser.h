#ifndef MU__PARSER__H__
#define MU__PARSER__H__

#include <stddef.h>
#include <stdint.h>

#define PARSER_WARNING__OVERFLOW        +1
#define PARSER_WARNING__FLOAT_PREFIX    +2
#define PARSER_ERROR__END_OF_LINE       -1
#define PARSER_ERROR__NO_DIGITS         -2
#define PARSER_ERROR__NO_EOL            -3
#define PARSER_ERROR__NO_FLOAT          -4

/*
 * Keyword tracker
 */

#define KW_TRACKER__IGNORE_CASE           1

struct keyword_desc
{
    const char * text;
    int id;
};

struct keyword_tracker_step
{
    uint64_t possible[256];
    struct keyword_tracker_step * next;
};


struct keyword_tracker
{
    void * data;
    const struct keyword_desc * keyword_list;
    const struct keyword_tracker_step * first_step;
};

const struct keyword_tracker * create_keyword_tracker(const struct keyword_desc * keyword_list, int flags);
void destroy_keyword_tracker(const struct keyword_tracker * const me);



/*
 * Character sets (256 bits)
 */

struct char_set
{
    uint64_t bits[4];
};

static inline int is_in_char_set(unsigned char ch, const struct char_set * set)
{
    int offset = ch % 64;
    size_t index = ch / 64;
    uint64_t mask = 1; mask <<= offset;
    return (set->bits[index] & mask) != 0;
}

static const struct char_set space_set = { { 0x00000001FFFFFFFE, 0, 0, 0 } };
static const struct char_set id_first_char_set =  { { 0, 0x07FFFFFE87FFFFFE, 0, 0 } };
static const struct char_set id_char_set =  { { 0x03FF000000000000, 0x07FFFFFE87FFFFFE, 0, 0 } };

static inline int is_space_char(unsigned char ch)
{
    return is_in_char_set(ch, &space_set);
}

static inline int is_first_id_char(unsigned char ch)
{
    return is_in_char_set(ch, &id_first_char_set);
}

static inline int is_id_char(unsigned char ch)
{
    return is_in_char_set(ch, &id_char_set);
}



/*
 * Line Parser
 */

struct line_parser
{
    const unsigned char * line;
    const unsigned char * current;
    const unsigned char * lexem_start;
};

static inline void parser_skip_spaces(struct line_parser * restrict const me)
{
    while (is_space_char(*me->current)) {
        me->current++;
    }
    me->lexem_start = me->current;
}

void parser_set_line(struct line_parser * restrict const me, const char * const line);
int parser_check_eol(struct line_parser * restrict const me);
int parser_is_text(const struct line_parser * const me, const char * const text);
int parser_try_int(struct line_parser * restrict const me, int * restrict const value);
int parser_read_last_int( struct line_parser * restrict const me, int * restrict const value);
int parser_read_float( struct line_parser * restrict const me, float * restrict const value);
int parser_read_keyword(struct line_parser * restrict const me, const struct keyword_tracker * const tracker);
int parser_read_id(struct line_parser * restrict const me);

/* Dependency */
void * multialloc(const size_t n, const size_t * const sizes,
    void * restrict * ptrs, const size_t granularity);

#endif
