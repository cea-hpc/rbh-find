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

#include <robinhood.h>
#include <robinhood/utils.h>

#include "rbh-find/actions.h"
#include "rbh-find/core.h"
#include "rbh-find/filters.h"
#include "rbh-find/parser.h"

int
main(int _argc, char *_argv[])
{
    struct find_context ctx = { .backend_count = 0, .action_done = false };
    struct rbh_filter_sort *sorts = NULL;
    struct rbh_filter *filter;
    size_t sorts_count = 0;
    int index;
    /* Discard the program's name */
    ctx.argc = _argc - 1;
    ctx.argv = &_argv[1];

    ctx.pre_action_callback = &core_pre_action;
    ctx.exec_action_callback = &core_exec_action;
    ctx.post_action_callback = &core_post_action;
    ctx.parse_predicate_callback = &core_parse_predicate;

    /* Parse the command line */
    for (index = 0; index < ctx.argc; index++) {
        if (str2command_line_token(ctx.argv[index]) != CLT_URI)
            break;
    }
    if (index == 0)
        ERROR(&ctx, EX_USAGE, 0, "missing at least one robinhood URI");

    ctx.backends = malloc(index * sizeof(*ctx.backends));
    if (ctx.backends == NULL)
        ERROR(&ctx, EXIT_FAILURE, errno, "malloc");

    for (int i = 0; i < index; i++) {
        ctx.backends[i] = rbh_backend_from_uri(ctx.argv[i]);
        ctx.backend_count++;
    }
    filter = parse_expression(&ctx, &index, NULL, &sorts, &sorts_count);
    if (index != ctx.argc)
        ERROR(&ctx, EX_USAGE, 0, "you have too many ')'");

    if (!ctx.action_done)
        find(&ctx, ACT_PRINT, &index, filter, sorts, sorts_count);
    free(filter);

    ctx_finish(&ctx);
    return EXIT_SUCCESS;
}
