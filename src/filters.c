/* This file is part of rbh-find
 * Copyright (C) 2019 Commissariat a l'energie atomique et aux energies
 *                    alternatives
 *
 * SPDX-License-Identifer: LGPL-3.0-or-later
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>
#include <errno.h>
#include <error.h>
#include <limits.h>
#include <stdlib.h>
#include <sysexits.h>
#include <ctype.h>

#include <sys/stat.h>
#ifndef HAVE_STATX
# include <robinhood/statx.h>
#endif

#include <robinhood/sstack.h>

#include "filters.h"
#include "utils.h"

static const struct rbh_filter_field predicate2filter_field[] = {
    [PRED_AMIN]     = { .fsentry = RBH_FP_STATX, .statx = STATX_ATIME, },
    [PRED_ATIME]    = { .fsentry = RBH_FP_STATX, .statx = STATX_ATIME, },
    [PRED_CMIN]     = { .fsentry = RBH_FP_STATX, .statx = STATX_CTIME, },
    [PRED_CTIME]    = { .fsentry = RBH_FP_STATX, .statx = STATX_CTIME, },
    [PRED_NAME]     = { .fsentry = RBH_FP_NAME, },
    [PRED_INAME]    = { .fsentry = RBH_FP_NAME, },
    [PRED_MMIN]     = { .fsentry = RBH_FP_STATX, .statx = STATX_MTIME, },
    [PRED_MTIME]    = { .fsentry = RBH_FP_STATX, .statx = STATX_MTIME, },
    [PRED_TYPE]     = { .fsentry = RBH_FP_STATX, .statx = STATX_TYPE },
    [PRED_PERM]     = { .fsentry = RBH_FP_STATX, .statx = STATX_MODE },
};

struct rbh_filter *
shell_regex2filter(enum predicate predicate, const char *shell_regex,
                   unsigned int regex_options)
{
    struct rbh_filter *filter;
    char *pcre;

    pcre = shell2pcre(shell_regex);
    if (pcre == NULL)
        error_at_line(EXIT_FAILURE, ENOMEM, __FILE__, __LINE__ - 2,
                      "converting %s into a Perl Compatible Regular Expression",
                      shell_regex);

    filter = rbh_filter_compare_regex_new(RBH_FOP_REGEX,
                                          &predicate2filter_field[predicate],
                                          pcre, regex_options);
    if (filter == NULL)
        error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__ - 2,
                      "building a regex filter for %s", pcre);
    free(pcre);
    return filter;
}

static struct rbh_filter *
filter_uint64_range_new(const struct rbh_filter_field *field, uint64_t start,
                        uint64_t end)
{
    struct rbh_filter *low, *high;

    low = rbh_filter_compare_uint64_new(RBH_FOP_STRICTLY_GREATER, field, start);
    if (low == NULL)
        error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__,
                      "rbh_filter_compare_time");

    high = rbh_filter_compare_uint64_new(RBH_FOP_STRICTLY_LOWER, field, end);
    if (high == NULL)
        error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__,
                      "rbh_filter_compare_time");

    return filter_and(low, high);
}

static struct rbh_filter *
timedelta2filter(enum predicate predicate, enum time_unit unit,
                 const char *_timedelta)
{
    const struct rbh_filter_field *field = &predicate2filter_field[predicate];
    const char *timedelta = _timedelta;
    char operator = *timedelta;
    struct rbh_filter *filter;
    unsigned long delta; /* in seconds */
    time_t now, then;
    int save_errno;

    switch (operator) {
    case '-':
    case '+':
        timedelta++;
    }

    /* Convert the time string to a number of seconds */
    save_errno = errno;
    errno = 0;
    delta = str2seconds(unit, timedelta);
    if ((errno == ERANGE && delta == ULONG_MAX) || (errno != 0 && delta == 0))
        error(EXIT_FAILURE, 0, "invalid argument `%s' to `%s'", _timedelta,
              predicate2str(predicate));
    errno = save_errno;

    /* Compute `then' */
    now = time(NULL);
    if (now < 0)
        error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__, "time");
    then = now - delta;

    switch (operator) {
    case '-':
        filter = rbh_filter_compare_uint64_new(RBH_FOP_STRICTLY_GREATER, field,
                                              then);
        if (filter == NULL)
            error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__,
                          "rbh_filter_compare_time_new");
        break;
    case '+':
        filter = rbh_filter_compare_uint64_new(RBH_FOP_STRICTLY_LOWER, field,
                                               then);
        if (filter == NULL)
            error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__,
                          "rbh_filter_compare_time_new");
        break;
    default:
        filter = filter_uint64_range_new(field, then - TIME_UNIT2SECONDS[unit],
                                         then);
        if (filter == NULL)
            error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__,
                          "filter_time_range_new");
        break;
    }

    return filter;
}

struct rbh_filter *
xmin2filter(enum predicate predicate, const char *minutes)
{
    return timedelta2filter(predicate, TU_MINUTE, minutes);
}

struct rbh_filter *
xtime2filter(enum predicate predicate, const char *days)
{
    return timedelta2filter(predicate, TU_DAY, days);
}

struct rbh_filter *
filetype2filter(const char *_filetype)
{
    struct rbh_filter *filter;
    int filetype;

    if (_filetype[0] == '\0' || _filetype[1] != '\0')
        error(EX_USAGE, 0, "arguments to -type should only contain one letter");

    switch (_filetype[0]) {
    case 'b':
        filetype = S_IFBLK;
        break;
    case 'c':
        filetype = S_IFCHR;
        break;
    case 'd':
        filetype = S_IFDIR;
        break;
    case 'f':
        filetype = S_IFREG;
        break;
    case 'l':
        filetype = S_IFLNK;
        break;
    case 'p':
        filetype = S_IFIFO;
        break;
    case 's':
        filetype = S_IFSOCK;
        break;
    default:
        error(EX_USAGE, 0, "unknown argument to -type: %s", _filetype);
        /* clang: -Wsometimes-unitialized: `filtetype` */
        __builtin_unreachable();
    }

    filter = rbh_filter_compare_int32_new(RBH_FOP_EQUAL,
                                          &predicate2filter_field[PRED_TYPE],
                                          filetype);
    if (filter == NULL)
        error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__,
                      "filter_compare_integer");

    return filter;
}

static int
parse_symbolic(const char *input, uint32_t *mode, const char **end)
{
    int user, group, other;
    uint32_t previous_flags;
    uint32_t usermask;
    uint32_t perm;
    int who, all;
    char c, op;
    int loop;

    user = group = other = 0;
    all = 0;
    loop = 1;
    perm = 0;
    previous_flags = 0;
    *end = input;
    usermask = 0;

    while (loop) {
        switch (*input) {
        case 'u':
            user = 1;
            break;
        case 'g':
            group = 1;
            break;
        case 'o':
            other = 1;
            break;
        case 'a':
            user = group = other = 1;
            all = 1;
            break;
        default:
            loop = 0;
            break;
        }

        if (loop)
            input++;
    }

    who = user || group || other;
    if (!who) {
        /* get the umask */
        usermask = umask(0022);
        umask(usermask);
        usermask &= 07777;
    }

    if (*input == '-' || *input == '+' || *input == '=')
        op = *input++;
    else
        /* operation is required */
        return -1;

    /* get the flags in mode */
    switch (*input) {
    case 'u':
        previous_flags = (*mode & 0700);
        perm |= user  ? previous_flags : 0;
        perm |= group ? (previous_flags >> 3) : 0;
        perm |= other ? (previous_flags >> 6) : 0;
        input++;
        goto write_perm;
    case 'g':
        previous_flags = (*mode & 0070);
        perm |= user  ? (previous_flags << 3) : 0;
        perm |= group ? previous_flags : 0;
        perm |= other ? (previous_flags >> 3) : 0;
        input++;
        goto write_perm;
    case 'o':
        previous_flags = (*mode & 0007);
        perm |= user  ? (previous_flags << 6) : 0;
        perm |= group ? (previous_flags << 3) : 0;
        perm |= other ? previous_flags : 0;
        input++;
        goto write_perm;
    default:
        break;
    }

    /* the part after the operator is optional
     * if empty perm = 0 and *mode is not modified
     */
    loop = 1;
    while (loop) {
        c = *input;

        switch (c) {
        case 'r':
            perm |= user  ? 0400 : 0;
            perm |= group ? 0040 : 0;
            perm |= other ? 0004 : 0;
            /* set read permission for uog except for umsk's permissions
             */
            perm |= who   ? 0 : (0444 & ~usermask);
            break;
        case 'w':
            perm |= user  ? 0200 : 0;
            perm |= group ? 0020 : 0;
            perm |= other ? 0002 : 0;
            /* set write permission for uog except for umsk's permissions
             */
            perm |= who   ? 0 : (0222 & ~usermask);
            break;
        case 'x':
            perm |= user  ? 0100 : 0;
            perm |= group ? 0010 : 0;
            perm |= other ? 0001 : 0;
            /* set execute permission for uog except for umsk's permissions
             */
            perm |= who   ? 0 : (0111 & ~usermask);
            break;
        case 'X':
            /* Adds execute permission to 'u', 'g' and/or 'o' if specified and
             * either 'u', 'g' or 'o' already has execute permissions.
             */
            if ((*mode & 0111) != 0) {
                perm |= user  ? 0100 : 0;
                perm |= group ? 0010 : 0;
                perm |= other ? 0001 : 0;
            }
            break;
        case 's':
            /* 's' is ignored if only 'o' is given, it's not an error */
            if (other && !group && !user)
                break;
            perm |= user  ? S_ISUID : 0;
            perm |= group ? S_ISGID : 0;
            break;
        case 't':
            /* 't' should be used when 'a' is given or who is empty */
            perm |= (!who || all) ? S_ISVTX : 0;
            /* using ugo with t is no an error but does nothing */
            break;
        default:
            loop = 0;
            break;
        }

        if (loop)
            input++;
    }

write_perm:
    /* uog flags after operator should only be one character long */
    if (previous_flags && (*input != '\0' && *input != ','))
        return -1;

    switch (op) {
    case '-':
        /* remove the flags from mode */
        *mode &= ~perm;
        break;
    case '+':
        /* add the flags to mode */
        *mode |= perm;
        break;
    case '=':
        if (perm != 0)
            /* set the flags of mode to perm */
            *mode = perm;
        break;
    }

    *end = input;
    return 0;
}

static int
str2mode(const char *input, uint32_t *mode)
{
    const char *iter;

    if (*input >= '0' && *input <= '7') {
        /* parse octal representation */
        char *end;

        iter = input;

        /* look for invalid digits in octal representation */
        while (isdigit(*iter))
            if (*iter++ > '7')
                return -1;

        errno = 0;
        *mode = strtoul(input, &end, 8);

        if (errno != 0 || *mode > 07777) {
            *mode = 0;
            return -1;
        }
    } else if (*input == '8' || *input == '9') {
        /* error: invalid octal number */
        return -1;
    } else {
        /* parse coma seperated list of symbolic representation */
        const char *end;
        int rc;

        *mode = 0;
        end = NULL;

        do {
            rc = parse_symbolic(input, mode, &end);
            if (rc)
                return -1;
            input = end+1;
        } while (*end == ',');

        if (*end != '\0')
            return -1;
    }

    return 0;
}

struct rbh_filter *
mode2filter(const char *modestr)
{
    struct rbh_filter *filter;
    uint32_t mode;
    int operator;

    if (*modestr == '\0')
        error(EX_USAGE, 0, "arguments to -perm should contain at least one "
                           "digit or a symblic mode");

    operator = RBH_FOP_EQUAL;
    if (*modestr == '/') {
        operator = RBH_FOP_BITS_ANY_SET;
        modestr++;
    }
    else if (*modestr == '-') {
        operator = RBH_FOP_BITS_ALL_SET;
        modestr++;
    }

    if (str2mode(modestr, &mode)) {
        error(EX_USAGE, 0, "invalid mode: %s", modestr);
    }

    filter = rbh_filter_compare_uint32_new(operator,
                                           &predicate2filter_field[PRED_PERM],
                                           mode);
    if (filter == NULL)
        error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__,
                      "filter_compare_integer");

    return filter;
}

static struct rbh_sstack *filters;

static void __attribute__((constructor))
init_filters(void)
{
    const int MIN_FILTER_ALLOC = 32;

    filters = rbh_sstack_new(MIN_FILTER_ALLOC * sizeof(struct rbh_filter *));
    if (filters == NULL)
        error(EXIT_FAILURE, errno, "rbh_sstack_new");
}

static void __attribute__((destructor))
exit_filters(void)
{
    struct rbh_filter **filter;
    size_t size;

    while (true) {
        int rc;

        filter = rbh_sstack_peek(filters, &size);
        if (size == 0)
            break;

        assert(size % sizeof(*filter) == 0);

        for (size_t i = 0; i < size / sizeof(*filter); i++, filter++)
            free(*filter);

        rc = rbh_sstack_pop(filters, size);
        assert(rc == 0);
    }
    rbh_sstack_destroy(filters);
}

static struct rbh_filter *
filter_compose(enum rbh_filter_operator op, struct rbh_filter *left,
               struct rbh_filter *right)
{
    const struct rbh_filter **array;
    struct rbh_filter *filter;

    assert(op == RBH_FOP_AND || op == RBH_FOP_OR);

    filter = malloc(sizeof(*filter));
    if (filter == NULL)
        error(EXIT_FAILURE, errno, "malloc");

    array = rbh_sstack_push(filters, NULL, sizeof(*array) * 2);
    if (array == NULL)
        error(EXIT_FAILURE, errno, "rbh_sstack_push");

    array[0] = left;
    array[1] = right;

    filter->op = op;
    filter->logical.filters = array;
    filter->logical.count = 2;

    return filter;
}

struct rbh_filter *
filter_and(struct rbh_filter *left, struct rbh_filter *right)
{
    return filter_compose(RBH_FOP_AND, left, right);
}

struct rbh_filter *
filter_or(struct rbh_filter *left, struct rbh_filter *right)
{
    return filter_compose(RBH_FOP_OR, left, right);
}

struct rbh_filter *
filter_not(struct rbh_filter *filter)
{
    struct rbh_filter *not;

    not = malloc(sizeof(*not));
    if (not == NULL)
        error(EXIT_FAILURE, errno, "malloc");

    not->logical.filters = rbh_sstack_push(filters, &filter, sizeof(filter));
    if (not->logical.filters == NULL)
        error(EXIT_FAILURE, errno, "rbh_sstack_push");

    not->op = RBH_FOP_NOT;
    not->logical.count = 1;

    return not;
}
