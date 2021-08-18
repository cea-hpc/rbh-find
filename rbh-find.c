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
#include <sysexits.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <robinhood.h>
#ifndef HAVE_STATX
# include <robinhood/statx.h>
#endif
#include <robinhood/utils.h>

#include "actions.h"
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

union action_arguments {
    /* ACT_FLS, ACT_FPRINT, ACT_FPRINT0, ACT_FPRINTF */
    FILE *file;
};

static size_t
_find(struct rbh_backend *backend, enum action action,
      const struct rbh_filter *filter, const struct rbh_filter_sort *sorts,
      size_t sorts_count, const union action_arguments *args)
{
    const struct rbh_filter_options OPTIONS = {
        .projection = {
            .fsentry_mask = RBH_FP_ALL,
            .statx_mask = STATX_ALL,
        },
        .sort = {
            .items = sorts,
            .count = sorts_count
        },
    };
    struct rbh_mut_iterator *fsentries;
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
            printf("%s\n", fsentry_path(fsentry));
            break;
        case ACT_PRINT0:
            printf("%s%c", fsentry_path(fsentry), '\0');
            break;
        case ACT_FPRINT:
            fprintf(args->file, "%s\n", fsentry_path(fsentry));
            break;
        case ACT_FPRINT0:
            fprintf(args->file, "%s%c", fsentry_path(fsentry), '\0');
            break;
        case ACT_LS:
            fsentry_print_ls_dils(fsentry);
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
find(enum action action, int *arg_idx, const struct rbh_filter *filter,
     const struct rbh_filter_sort *sorts, size_t sorts_count)
{
    union action_arguments args;
    const char *filename;
    int i = *arg_idx;
    size_t count = 0;

    did_something = true;

    switch (action) {
    case ACT_FPRINT:
    case ACT_FPRINT0:
        if (i >= argc)
            error(EX_USAGE, 0, "missing argument to `%s'", action2str(action));

        filename = argv[i++];
        args.file = fopen(filename, "w");
        if (args.file == NULL)
            error(EXIT_FAILURE, errno, "fopen: %s", filename);
        break;
    default:
        break;
    }

    for (size_t i = 0; i < backend_count; i++)
        count += _find(backends[i], action, filter, sorts, sorts_count, &args);

    switch (action) {
    case ACT_COUNT:
        printf("%lu matching entries\n", count);
        break;
    case ACT_FPRINT:
    case ACT_FPRINT0:
        if (fclose(args.file))
            error(EXIT_FAILURE, errno, "fclose: %s", filename);
        break;
    default:
        break;
    }

    *arg_idx = i;
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
 * @param arg_idx       a pointer to the index of argv to start parsing at
 * @param _filter       a filter (the part of the cli parsed by the caller)
 * @param sorts         an array of filtering options
 * @param sorts_count   the size of \p sorts
 *
 * @return              a filter that represents the parsed expression
 *
 * Note this function is recursive and will call find() itself if it parses an
 * action
 */
static struct rbh_filter *
parse_expression(int *arg_idx, const struct rbh_filter *_filter,
                 struct rbh_filter_sort **sorts, size_t *sorts_count)
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
        bool ascending = true;

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
            tmp = parse_expression(&i, &negated_left_filter, sorts,
                                   sorts_count);
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
            tmp = parse_expression(&i, &left_filter, sorts, sorts_count);
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
        case CLT_RSORT:
            /* Set a descending sort option */
            ascending = false;
            __attribute__((fallthrough));
        case CLT_SORT:
            /* Build an options filter from the sort command and its arguments */
            if (i + 1 >= argc)
                error(EX_USAGE, 0, "missing argument to '%s'", argv[i]);
            *sorts = sort_options_append(*sorts, (*sorts_count)++,
                                         str2field(argv[++i]), ascending);
            break;
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
            /* Consume the action token */
            i++;
            find(str2action(argv[i - 1]), &i, &left_filter, *sorts,
                 *sorts_count);
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
    struct rbh_filter_sort *sorts = NULL;
    size_t sorts_count = 0;
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

    filter = parse_expression(&index, NULL, &sorts, &sorts_count);
    if (index != argc)
        error(EX_USAGE, 0, "you have too many ')'");

    if (!did_something)
        find(ACT_PRINT, &index, filter, sorts, sorts_count);
    free(filter);

    return EXIT_SUCCESS;
}
