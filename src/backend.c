/* This file is part of rbh-find
 * Copyright (C) 2022 Commissariat a l'energie atomique et aux energies
 *                    alternatives
 *
 * SPDX-License-Identifer: LGPL-3.0-or-later
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "rbh-find/backend.h"

void
exit_backends(struct find_context *ctx)
{
    for (size_t i = 0; i < ctx->backend_count; i++)
        rbh_backend_destroy(ctx->backends[i]);
    free(ctx->backends);
}

size_t
_find(struct find_context *ctx, int backend_index, enum action action,
      const struct rbh_filter *filter, const struct rbh_filter_sort *sorts,
      size_t sorts_count, const union action_arguments *args,
      int (*action_callback)(struct find_context*, enum action,
                             struct rbh_fsentry*,
                             const union action_arguments*))
{
    const struct rbh_filter_options OPTIONS = {
        .projection = {
            .fsentry_mask = RBH_FP_ALL,
            .statx_mask = RBH_STATX_ALL,
        },
        .sort = {
            .items = sorts,
            .count = sorts_count
        },
    };
    struct rbh_mut_iterator *fsentries;
    size_t count = 0;

    fsentries = rbh_backend_filter(ctx->backends[backend_index], filter,
                                   &OPTIONS);
    if (fsentries == NULL)
        ERROR_AT_LINE(ctx, EXIT_FAILURE, errno, __FILE__, __LINE__,
                      "filter_fsentries");

    do {
        struct rbh_fsentry *fsentry;

        do {
            errno = 0;
            fsentry = rbh_mut_iter_next(fsentries);
        } while (fsentry == NULL && errno == EAGAIN);

        if (fsentry == NULL)
            break;

        count += action_callback(ctx, action, fsentry, args);
        free(fsentry);
    } while (true);

    if (errno != ENODATA)
        ERROR_AT_LINE(ctx, EXIT_FAILURE, errno, __FILE__, __LINE__,
                      "rbh_mut_iter_next");

    rbh_mut_iter_destroy(fsentries);

    return count;
}

/**
 * parse_expression - parse a find expression (predicates / operators / actions)
 *
 * @param ctx           find's context for this execution
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
struct rbh_filter *
parse_expression(struct find_context *ctx, int *arg_idx,
                 const struct rbh_filter *_filter,
                 struct rbh_filter_sort **sorts, size_t *sorts_count,
                 struct rbh_filter* (*predicate_callback)(struct find_context*,
                                                          int *),
                 void (*find_callback)(struct find_context*, enum action, int*,
                                       const struct rbh_filter*,
                                       const struct rbh_filter_sort*,
                                       size_t))
{
    static enum command_line_token token = CLT_URI;
    struct rbh_filter *filter = NULL;
    bool negate = false;
    int i;

    for (i = *arg_idx; i < ctx->argc; i++) {
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

        token = str2command_line_token(ctx->argv[i]);
        switch (token) {
        case CLT_URI:
            ERROR(ctx, EX_USAGE, 0, "paths must preceed expression: %s",
                  ctx->argv[i]);
            __builtin_unreachable();
        case CLT_AND:
        case CLT_OR:
            switch (previous_token) {
            case CLT_ACTION:
            case CLT_PREDICATE:
            case CLT_PARENTHESIS_CLOSE:
                break;
            default:
                ERROR(ctx, EX_USAGE, 0,
                      "invalid expression; you have used a binary operator '%s' with nothing before it.",
                      ctx->argv[i]);
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
            tmp = parse_expression(ctx, &i, &negated_left_filter, sorts,
                                   sorts_count, predicate_callback,
                                   find_callback);
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
            tmp = parse_expression(ctx, &i, &left_filter, sorts, sorts_count,
                                   predicate_callback, find_callback);
            if (i >= ctx->argc || token != CLT_PARENTHESIS_CLOSE)
                ERROR(ctx, EX_USAGE, 0,
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
                ERROR(ctx, EXIT_FAILURE, 0, "invalid expression; empty parentheses are not allowed.");
            /* End of a sub-expression, update arg_idx and return */
            *arg_idx = i;
            return filter;
        case CLT_RSORT:
            /* Set a descending sort option */
            ascending = false;
            __attribute__((fallthrough));
        case CLT_SORT:
            /* Build an options filter from the sort command and its arguments */
            if (i + 1 >= ctx->argc)
                ERROR(ctx, EX_USAGE, 0, "missing argument to '%s'",
                      ctx->argv[i]);
            *sorts = sort_options_append(*sorts, (*sorts_count)++,
                                         str2field(ctx->argv[++i]), ascending);
            break;
        case CLT_PREDICATE:
            /* Build a filter from the predicate and its arguments */
            tmp = predicate_callback(ctx, &i);
            if (negate) {
                tmp = filter_not(tmp);
                negate = false;
            }

            filter = filter_and(filter, tmp);
            break;
        case CLT_ACTION:
            /* Consume the action token */
            i++;
            ctx->action_done = true;
            find_callback(ctx, str2action(ctx->argv[i - 1]), &i, &left_filter,
                          *sorts, *sorts_count);
            break;
        }
    }

    *arg_idx = i;
    return filter;
}
