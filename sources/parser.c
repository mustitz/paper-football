#include "parser.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>



/*
 * Keyword tracker.
 */

static inline uint64_t get_possible(const struct keyword_tracker_step * restrict * ptr, const unsigned char ch)
{
    const struct keyword_tracker_step * const current = *ptr;
    *ptr = current->next;
    return current->possible[ch];
}

const struct keyword_tracker * create_keyword_tracker(const struct keyword_desc * const keyword_list, const int flags)
{
    int keyword_count = 0;
    size_t max_len = 0;
    size_t text_lengths[64];

    for (const struct keyword_desc * kw = keyword_list; kw->text; ++kw) {
        if (keyword_count >= 64) {
            return NULL;
        }
        const size_t tmp = text_lengths[keyword_count++] = strlen(kw->text);
        if (tmp > max_len) {
            max_len = tmp;
        }
    }

    ++max_len;

    size_t sizes[max_len+1];
    for (int i=0; i<max_len; ++i)
        sizes[i] = sizeof(struct keyword_tracker_step);
    sizes[max_len] = sizeof(struct keyword_tracker);

    void * ptrs[max_len+1];
    void * const data = multialloc(max_len+1, sizes, ptrs, 128);
    if (data == NULL) {
        return NULL;
    }

    struct keyword_tracker * restrict const me = ptrs[max_len];
    me->data = data;
    me->keyword_list = keyword_list;

    struct keyword_tracker_step * restrict prev_step = ptrs[0];
    memset(prev_step->possible, 0, sizeof(prev_step->possible));

    prev_step->next = prev_step;
    for (size_t i=1; i<max_len; ++i) {
        struct keyword_tracker_step * restrict const step = ptrs[i];
        memset(step->possible, 0, sizeof(step->possible));
        step->next = prev_step;
        prev_step = step;
    }
    me->first_step = prev_step;

    struct keyword_tracker_step * restrict step = prev_step;
    for (size_t i=0; i<max_len; ++i) {
        for (int j=0; j<keyword_count; ++j) {
            if (i > text_lengths[j]) {
                continue;
            }

            const uint64_t mask = 1ull << j;
            if (i < text_lengths[j]) {
                const unsigned char ch = keyword_list[j].text[i];
                step->possible[ch] |= mask;
                if (flags & KW_TRACKER__IGNORE_CASE) {
                    unsigned char ch2 = '\0';
                    if (ch >= 'a' && ch <= 'z') {
                        ch2 = ch -'a' + 'A';
                    }
                    if (ch >= 'A' && ch <= 'Z') {
                        ch2 = ch - 'A' + 'a';
                    }
                    if (ch2) {
                        step->possible[ch2] |= mask;
                    }
                }
                continue;
            }

            unsigned char ch = 0;
            do {
                if (!is_id_char(ch)) {
                    step->possible[ch] |= mask;
                }
            } while (++ch != 0);
        }
        step = step->next;
    }

    return me;
}

void destroy_keyword_tracker(const struct keyword_tracker * const me)
{
    free(me->data);
}



/*
 * Line parser.
 */

void parser_set_line(struct line_parser * restrict const me, const char * const line)
{
    const unsigned char * const unsigned_line = (const unsigned char *)line;
    me->line = me->current = me->lexem_start = unsigned_line;
}

int parser_check_eol(struct line_parser * restrict const me)
{
    parser_skip_spaces(me);
    return *me->current == '#' || *me->current == '\0';
}

int parser_is_text(const struct line_parser * const me, const char * text)
{
    const size_t len = me->current - me->lexem_start;
    const char * const lexem_start = (const char *)me->lexem_start;
    if (strncmp(lexem_start, text, len) != 0) {
        return 0;
    }

    if (text[len] != '\0') {
        return 0;
    }

    return 1;
}

int parser_try_int(struct line_parser * restrict const me, int * restrict const value)
{
    if (*me->current == '\0') {
        *value = 0;
        return PARSER_ERROR__END_OF_LINE;
    }

    const struct line_parser saved = *me;

    int sign = +1;
    if (*me->current == '-') {
        sign = -1;
        ++me->current;
    }

    if (*me->current < '0' || *me->current > '9') {
        *me = saved;
        *value = 0;
        return PARSER_ERROR__NO_DIGITS;
    }

    int result = *me->current++ - '0';
    result *= sign;

    int is_overflow = 0;
    for (;;) {
        if (*me->current < '0' || *me->current > '9') {
            if (!is_overflow) {
                *value = result;
                return 0;
            } else {
                *value = sign > 0 ? INT_MAX : INT_MIN;
                return PARSER_WARNING__OVERFLOW;
            }
        }

        int new_result = 10*result;

        if (sign > 0) {
            new_result += *me->current++ - '0';
            is_overflow |= new_result < result;
        } else {
            new_result -= *me->current++ - '0';
            is_overflow |= new_result > result;
        }

        result = new_result;
    }
}

int parser_read_last_int(
    struct line_parser * restrict const me,
    int * restrict const value)
{
    parser_skip_spaces(me);

    int result;
    const int err = parser_try_int(me, &result);
    if (err != 0) {
        return err;
    }

    if (!parser_check_eol(me)) {
        return PARSER_ERROR__NO_EOL;
    }

    *value = result;
    return 0;
}

int parser_read_float(
    struct line_parser * restrict const me,
    float * restrict const value)
{
    const unsigned char * const orig = me->current;
    parser_skip_spaces(me);

    const char * const float_str = (const char *)me->current;
    char * endptr;
    *value = strtof(float_str, &endptr);
    const int len = endptr - float_str;
    if (len == 0) {
        me->current = orig;
        return PARSER_ERROR__NO_FLOAT;
    }
    me->current += len;

    const unsigned char * const saved = me->current;
    if (parser_check_eol(me)) {
        return 0;
    }

    return me->current == saved ? PARSER_WARNING__FLOAT_PREFIX : 0;
}

int parser_read_keyword(struct line_parser * restrict const me,
    const struct keyword_tracker * const tracker)
{
    const struct keyword_tracker_step * step = tracker->first_step;

    me->lexem_start = me->current;
    if (!is_first_id_char(*me->current)) {
        return -1;
    }

    uint64_t possible = get_possible(&step, *me->current);

    for (;;) {
        ++me->current;
        possible &= get_possible(&step, *me->current);
        if (!is_id_char(*me->current)) {
            break;
        }
    }

    if (possible == 0) {
        return 0;
    }

    const int index = __builtin_ctzll(possible);
    return tracker->keyword_list[index].id;
}

int parser_read_id(struct line_parser * restrict const me)
{
    me->lexem_start = me->current;

    if (!is_first_id_char(*me->current)) {
        return -1;
    }

    for (;;) {
        ++me->current;
        if (!is_id_char(*me->current)) {
            return 0;
        }
    }
}



#ifdef MAKE_CHECK

#include "insider.h"

#define KW_I           5
#define KW_IF         10
#define KW_IFNULL     20
#define KW_IF_NULL    20
#define KW_IFZERO     25
#define KW_IF_ZERO    25
#define KW_TEST      999

#define ITEM(name) { #name, KW_##name }
struct keyword_desc test_keywords[] = {
    ITEM(I), ITEM(IF), ITEM(TEST),
    ITEM(IFNULL), ITEM(IFZERO),
    ITEM(IF_NULL), ITEM(IF_ZERO),
    { NULL, 0 }
};
#undef ITEM

static int test_skip_spaces(const char * line, int first_non_space_ch)
{
    struct line_parser line_parser;
    struct line_parser * restrict lp = &line_parser;
    parser_set_line(lp, line);

    parser_skip_spaces(lp);

    if (*lp->current != first_non_space_ch) {
        test_fail(
            "parser_skip_spaces is not working:\n"
            "  line = `%s'\n"
            "  current = `%s'\n"
            "  non space char expected = `%c', actual `%c'.",
            line, lp->current, first_non_space_ch, *lp->current
        );
    }

    return 0;
}

static int test_read_keyword(
    const struct keyword_tracker * restrict const tracker,
    const char * const line,
    const int expected_keyword_id,
    const unsigned char next_ch
) {
    struct line_parser line_parser;
    struct line_parser * restrict const lp = &line_parser;
    parser_set_line(lp, line);

    const int actual_keyword_id = parser_read_keyword(lp, tracker);

    if (*lp->current != next_ch) {
        test_fail(
            "test_read_keyword is not working:\n"
            "  line = `%s'\n"
            "  current = `%s'\n"
            "  next char expected = `%c', actual `%c'.",
            line, lp->current, next_ch, *lp->current
        );
    }

    if (actual_keyword_id != expected_keyword_id) {
        test_fail(
            "test_read_keyword is not working:\n"
            "  line = `%s'\n"
            "  current = `%s'\n"
            "  keyword_id expected = %d, actual = %d.",
            line, lp->current, expected_keyword_id, actual_keyword_id
        );
    }

    return 0;
}

static int test_try_int(
    const char * const line,
    const int expected_value,
    const int expected_err,
    const unsigned char next_ch
) {
    struct line_parser line_parser;
    struct line_parser * restrict const lp = &line_parser;
    parser_set_line(lp, line);

    int value;
    const int err = parser_try_int(lp, &value);

    const int is_ok = 1
        && value == expected_value
        && err == expected_err
        && next_ch == *lp->current
    ;

    if (!is_ok) {
        test_fail(
            "test_try_int is not working:\n"
            "  line = `%s'\n"
            "  current = `%s', next char = `%c'\n"
            "  value expected %d, actual %d,\n"
            "  error expected %d, actual %d.",
            line, lp->current, next_ch,
            expected_value, value, expected_err, err
        );
    }

    return 0;
}

int test_parser(void)
{
    test_skip_spaces("   123", '1');
    test_skip_spaces(" \t \n\r 123", '1');
    test_skip_spaces("", '\0');
    test_skip_spaces("   ", '\0');
    test_skip_spaces("123", '1');

    const struct keyword_tracker * restrict const tracker = create_keyword_tracker(test_keywords, 0);
    if (tracker == NULL) {
        test_fail("build_keyword_tracker returns NULL.");
    }
    test_read_keyword(tracker, "I ", KW_I, ' ');
    test_read_keyword(tracker, "TEST ", KW_TEST, ' ');
    test_read_keyword(tracker, "IF ", KW_IF, ' ');
    test_read_keyword(tracker, "IF_NULL ", KW_IF_NULL, ' ');
    test_read_keyword(tracker, "IFNULL ", KW_IFNULL, ' ');
    test_read_keyword(tracker, "IFZERO ", KW_IFZERO, ' ');
    test_read_keyword(tracker, "IF_ZERO ", KW_IF_ZERO, ' ');
    test_read_keyword(tracker, "IF_ZERO$", KW_IF_ZERO, '$');
    test_read_keyword(tracker, "IF_ZERO", KW_IF_ZERO, '\0');
    test_read_keyword(tracker, "XXX$", 0, '$');
    test_read_keyword(tracker, "XIF", 0, '\0');
    test_read_keyword(tracker, "IFF ", 0, ' ');

    test_try_int("123", 123, 0, '\0');
    test_try_int("124$", 124, 0, '$');
    test_try_int("129 ", 129, 0, ' ');
    test_try_int("128ull", 128, 0, 'u');
    test_try_int("-321", -321, 0, '\0');
    test_try_int("-321", -321, 0, '\0');
    test_try_int("2147483647", 2147483647, 0, '\0');
    test_try_int("-2147483647", -2147483647, 0, '\0');
    test_try_int("-2147483648", -2147483648, 0, '\0');
    test_try_int("", 0, PARSER_ERROR__END_OF_LINE, '\0');
    test_try_int("xxx", 0, PARSER_ERROR__NO_DIGITS, 'x');
    test_try_int("-xxx", 0, PARSER_ERROR__NO_DIGITS, '-');
    test_try_int(" 234", 0, PARSER_ERROR__NO_DIGITS, ' ');
    test_try_int("2147483648", INT_MAX, PARSER_WARNING__OVERFLOW, '\0');
    test_try_int("-2147483649", INT_MIN, PARSER_WARNING__OVERFLOW, '\0');
    test_try_int("-77777777777777$", INT_MIN, PARSER_WARNING__OVERFLOW, '$');
    test_try_int(" 234", 0, PARSER_ERROR__NO_DIGITS, ' ');

    destroy_keyword_tracker(tracker);
    return 0;
}

#endif
