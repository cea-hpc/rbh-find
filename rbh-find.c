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
#include <dlfcn.h>
#include <errno.h>
#include <error.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>

#include <sys/stat.h>
#include <time.h>

#include <robinhood.h>
#ifndef HAVE_STATX
# include <robinhood/statx.h>
#endif
#include <robinhood/utils.h>

#include "filters.h"
#include "parser.h"

static size_t backend_count = 0;
static struct rbh_backend **backends;

static void __attribute__((destructor))
exit_backends(void)
{
    for (size_t i = 0; i < backend_count; i++)
        rbh_backend_destroy(backends[i]);
    free(backends);
}

/* Mode string is: "rwxrwxrwx" */
const int MODE_STR_LENGTH = 10;

/* Timestamp string is: "Jan 31 12:00:00" or "Jan 31 2000" */
const int DATETIME_STR_LENGTH = 16;

/**
 * duration2date - convert a fsentry mtime to a timestamp string
 *
 * @param buf      a pointer to an empty buffer
 * @param mtime    a mtime in seconds
 *
 * @return         a pointer to a string representing the last modified
 *                 datetime
 */
char *
duration2date(char *buf, uint64_t mtime)
{
    time_t duration = mtime;
    struct tm *datetime;
    struct tm now;
    time_t tmp;

    tmp = time(NULL);
    if (localtime_r(&tmp, &now) == NULL)
        error(EXIT_FAILURE, EINVAL, "localtime_r");;

    if (duration < 0)
        duration = -duration;

    datetime = localtime(&duration);

    if (datetime == NULL)
        error(EXIT_FAILURE, EINVAL, "localtime");
    else if (datetime->tm_year < now.tm_year)
        strftime(buf, DATETIME_STR_LENGTH, "%b %e %G", datetime);
    else
        strftime(buf, DATETIME_STR_LENGTH, "%b %e %T", datetime);

    return buf;
}

/* The special bits. If set, display SMODE0/1 instead of MODE0/1 */
static const mode_t SBIT[] = {
    0, 0, S_ISUID,
    0, 0, S_ISGID,
    0, 0, S_ISVTX
};

/* The 9 mode bits to test */
static const mode_t MBIT[] = {
    S_IRUSR, S_IWUSR, S_IXUSR,
    S_IRGRP, S_IWGRP, S_IXGRP,
    S_IROTH, S_IWOTH, S_IXOTH
};

static const char MODE1[] = "rwxrwxrwx";
static const char MODE0[] = "---------";
static const char SMODE1[] = "..s..s..t";
static const char SMODE0[] = "..S..S..T";

/**
 * mode_string - return the standard ls-like mode string from a file mode
 *
 * @param buf               a pointer to an empty buffer
 * @param decimal_mode      a decimal representing the mode of a fsentry
 *
 * @return                  a pointer to a string representing the mode
 */
char *
mode_string(char *buf, uint16_t decimal_mode)
{
    mode_t mode = decimal_mode;
    int i;

    for (i = 0; i < 9; i++) {
        if (mode & SBIT[i])
            buf[i] = (mode & MBIT[i]) ? SMODE1[i] : SMODE0[i];
        else
            buf[i] = (mode & MBIT[i]) ? MODE1[i] : MODE0[i];
    }
    buf[i] = '\0';
    return buf;
}

static const char *
fsentry_path(const struct rbh_fsentry *fsentry)
{
    if (!(fsentry->mask & RBH_FP_NAMESPACE_XATTRS))
        return NULL;

    for (size_t i = 0; i < fsentry->xattrs.ns.count; i++) {
        const struct rbh_value_pair *pair = &fsentry->xattrs.ns.pairs[i];

        if (strcmp(pair->key, "path"))
            continue;

        if (pair->value->type != RBH_VT_STRING)
            /* XXX: should probably say something... */
            continue;

        return pair->value->string;
    }

    return NULL;
}

static size_t
_find(struct rbh_backend *backend, enum action action,
      const struct rbh_filter *filter)
{
    const struct rbh_filter_options OPTIONS = {
        .projection = {
            .fsentry_mask = RBH_FP_ALL,
            .statx_mask = STATX_ALL,
        },
    };
    struct rbh_mut_iterator *fsentries;
    char datetime[DATETIME_STR_LENGTH];
    char mode[MODE_STR_LENGTH];
    size_t count = 0;

    fsentries = rbh_backend_filter(backend, filter, &OPTIONS);
    if (fsentries == NULL)
        error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__,
                      "filter_fsentries");

    do {
        struct rbh_fsentry *fsentry;

        do {
            errno = 0;
            fsentry = rbh_mut_iter_next(fsentries);
        } while (fsentry == NULL && errno == EAGAIN);

        if (fsentry == NULL)
            break;

        switch (action) {
        case ACT_PRINT:
            /* XXX: glibc's printf() handles printf("%s", NULL) pretty well, but
             *      I do not think this is part of any standard.
             */
            printf("%s%c", fsentry_path(fsentry), '\n');
            break;
        case ACT_PRINT0:
            printf("%s%c", fsentry_path(fsentry), '\0');
            break;
        case ACT_LS:
            printf("%lld %lld %s %d %d %d %lld %s %s%c",
                   fsentry->statx->stx_ino,
                   fsentry->statx->stx_blocks,
                   mode_string(mode,
                               fsentry->statx->stx_mode),
                   fsentry->statx->stx_nlink,
                   fsentry->statx->stx_uid,
                   fsentry->statx->stx_gid,
                   fsentry->statx->stx_size,
                   duration2date(datetime,
                                 fsentry->statx->stx_mtime.tv_sec),
                   fsentry_path(fsentry),
                   '\n');
            break;
        case ACT_COUNT:
            count++;
            break;
        case ACT_QUIT:
            exit(0);
            break;
        default:
            error(EXIT_FAILURE, ENOSYS, "%s", action2str(action));
            break;
        }
        free(fsentry);
    } while (true);

    if (errno != ENODATA)
        error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__,
                      "rbh_mut_iter_next");

    rbh_mut_iter_destroy(fsentries);

    return count;
}

static int argc;
static char **argv;
static bool did_something = false;

static void
find(enum action action, const struct rbh_filter *filter)
{
    size_t count = 0;

    did_something = true;

    for (size_t i = 0; i < backend_count; i++)
        count += _find(backends[i], action, filter);

    switch (action) {
    case ACT_COUNT:
        printf("%lu matching entries\n", count);
        break;
    default:
        break;
    }
}

static struct rbh_filter *
parse_predicate(int *arg_idx)
{
    enum predicate predicate;
    struct rbh_filter *filter;
    int i = *arg_idx;

    predicate = str2predicate(argv[i]);

    if (i + 1 >= argc)
        error(EX_USAGE, 0, "missing argument to `%s'", argv[i]);

    /* In the following block, functions should call error() themselves rather
     * than returning.
     *
     * Errors are most likely fatal (not recoverable), and this allows for
     * precise and meaningul error messages.
     */
    switch (predicate) {
    case PRED_AMIN:
    case PRED_MMIN:
    case PRED_CMIN:
        filter = xmin2filter(predicate, argv[++i]);
        break;
    case PRED_ATIME:
    case PRED_MTIME:
    case PRED_CTIME:
        filter = xtime2filter(predicate, argv[++i]);
        break;
    case PRED_NAME:
        filter = shell_regex2filter(predicate, argv[++i], 0);
        break;
    case PRED_INAME:
        filter = shell_regex2filter(predicate, argv[++i],
                                    RBH_RO_CASE_INSENSITIVE);
        break;
    case PRED_TYPE:
        filter = filetype2filter(argv[++i]);
        break;
    case PRED_SIZE:
        filter = filesize2filter(argv[++i]);
        break;
    case PRED_PERM:
        filter = mode2filter(argv[++i]);
        break;
    default:
        error(EXIT_FAILURE, ENOSYS, "%s", argv[i]);
        /* clang: -Wsometimes-unitialized: `filter` */
        __builtin_unreachable();
    }
    assert(filter != NULL);

    *arg_idx = i;
    return filter;
}

/**
 * parse_expression - parse a find expression (predicates / operators / actions)
 *
 * @param arg_idx   a pointer to the index of argv to start parsing at
 * @param _filter   a filter (the part of the cli parsed by the caller)
 *
 * @return          a filter that represents the parsed expression
 *
 * Note this function is recursive and will call find() itself if it parses an
 * action
 */
static struct rbh_filter *
parse_expression(int *arg_idx, const struct rbh_filter *_filter)
{
    static enum command_line_token token = CLT_URI;
    struct rbh_filter *filter = NULL;
    bool negate = false;
    int i;

    for (i = *arg_idx; i < argc; i++) {
        const struct rbh_filter *left_filters[2] = {filter, _filter};
        const struct rbh_filter left_filter = {
            .op = RBH_FOP_AND,
            .logical = {
                .filters = left_filters,
                .count = 2,
            },
        };
        const struct rbh_filter *ptr_to_left_filter = &left_filter;
        struct rbh_filter negated_left_filter = {
            .op = RBH_FOP_NOT,
            .logical = {
                .filters = &ptr_to_left_filter,
                .count = 1,
            },
        };
        enum command_line_token previous_token = token;
        struct rbh_filter *tmp;

        token = str2command_line_token(argv[i]);
        switch (token) {
        case CLT_URI:
            error(EX_USAGE, 0, "paths must preceed expression: %s", argv[i]);
            __builtin_unreachable();
        case CLT_AND:
        case CLT_OR:
            switch (previous_token) {
            case CLT_ACTION:
            case CLT_PREDICATE:
            case CLT_PARENTHESIS_CLOSE:
                break;
            default:
                error(EX_USAGE, 0,
                      "invalid expression; you have used a binary operator '%s' with nothing before it.",
                      argv[i]);
            }

            /* No further processing needed for CLT_AND */
            if (token == CLT_AND)
                break;

            /* The -o/-or operator is tricky to implement!
             *
             * It works this way: any entry that does not match the left
             * condition is checked against the right one. Readers should note
             * that an entry that matches the left condition _is not checked_
             * against the right condition.
             *
             * GNU-find can probably do this in a single filesystem scan, but we
             * cannot. We have to build a filter for the right condition that
             * excludes entries matched by the left condition.
             *
             * Basically, when we read: "<cond-A> -o <cond-B>", we
             * translate it to "<cond-A> -o (! <cond-A> -a <cond-B>)"
             *
             * An example might help:
             * -name -a -or -name -b <=> any entry whose name matches 'a' or
             *                           doesn't match 'a' but matches 'b'
             */

            /* Consume the -o/-or token */
            i++;

            /* Parse the filter at the right of -o/-or */
            tmp = parse_expression(&i, &negated_left_filter);
            /* parse_expression() returned, so it must have seen a closing
             * parenthesis or reached the end of the command line, we should
             * return here too.
             */

            /* "OR" the part of the left filter we parsed ourselves (ie. not
             * `_filter') and the right filter.
            */
            filter = filter_or(filter, tmp);

            /* Update arg_idx and return */
            *arg_idx = i;
            return filter;
        case CLT_NOT:
            negate = !negate;
            break;
        case CLT_PARENTHESIS_OPEN:
            /* Consume the ( token */
            i++;

            /* Parse the sub-expression */
            tmp = parse_expression(&i, &left_filter);
            if (i >= argc || token != CLT_PARENTHESIS_CLOSE)
                error(EX_USAGE, 0,
                      "invalid expression; I was expecting to find a ')' somewhere but did not see one.");

            /* Negate the sub-expression's filter, if need be */
            if (negate) {
                tmp = filter_not(tmp);
                negate = false;
            }

            /* Build the resulting filter and continue */
            filter = filter_and(filter, tmp);
            break;
        case CLT_PARENTHESIS_CLOSE:
            if (previous_token == CLT_PARENTHESIS_OPEN)
                error(EXIT_FAILURE, 0, "invalid expression; empty parentheses are not allowed.");
            /* End of a sub-expression, update arg_idx and return */
            *arg_idx = i;
            return filter;
        case CLT_PREDICATE:
            /* Build a filter from the predicate and its arguments */
            tmp = parse_predicate(&i);
            if (negate) {
                tmp = filter_not(tmp);
                negate = false;
            }

            filter = filter_and(filter, tmp);
            break;
        case CLT_ACTION:
            find(str2action(argv[i]), &left_filter);
            break;
        }
    }

    *arg_idx = i;
    return filter;
}

int
main(int _argc, char *_argv[])
{
    struct rbh_filter *filter;
    int index;

    /* Discard the program's name */
    argc = _argc - 1;
    argv = &_argv[1];

    /* Parse the command line */
    for (index = 0; index < argc; index++) {
        if (str2command_line_token(argv[index]) != CLT_URI)
            break;
    }
    if (index == 0)
        error(EX_USAGE, 0, "missing at least one robinhood URI");

    backends = malloc(index * sizeof(*backends));
    if (backends == NULL)
        error(EXIT_FAILURE, errno, "malloc");

    for (int i = 0; i < index; i++) {
        backends[i] = rbh_backend_from_uri(argv[i]);
        backend_count++;
    }

    filter = parse_expression(&index, NULL);
    if (index != argc)
        error(EX_USAGE, 0, "you have too many ')'");

    if (!did_something)
        find(ACT_PRINT, filter);
    free(filter);

    return EXIT_SUCCESS;
}
