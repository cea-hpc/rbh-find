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
#include "rbh-find/backend.h"
#include "rbh-find/filters.h"
#include "rbh-find/parser.h"

static int
exec_action(struct find_context *ctx, enum action action,
            struct rbh_fsentry *fsentry, const union action_arguments *args)
{
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
    case ACT_FLS:
        fsentry_print_ls_dils(args->file, fsentry);
        break;
    case ACT_FPRINT:
        fprintf(args->file, "%s\n", fsentry_path(fsentry));
        break;
    case ACT_FPRINT0:
        fprintf(args->file, "%s%c", fsentry_path(fsentry), '\0');
        break;
    case ACT_LS:
        fsentry_print_ls_dils(stdout, fsentry);
        break;
    case ACT_COUNT:
        return 1;
        break;
    case ACT_QUIT:
        exit_backends(ctx);
        exit(0);
        break;
    default:
        ERROR(ctx, EXIT_FAILURE, ENOSYS, "%s", action2str(action));
        break;
    }

    return 0;
}

static void
find(struct find_context *ctx, enum action action, int *arg_idx,
     const struct rbh_filter *filter, const struct rbh_filter_sort *sorts,
     size_t sorts_count)
{
    union action_arguments args;
    const char *filename;
    int i = *arg_idx;
    size_t count = 0;

    switch (action) {
    case ACT_FLS:
    case ACT_FPRINT:
    case ACT_FPRINT0:
        if (i >= ctx->argc)
            ERROR(ctx, EX_USAGE, 0, "missing argument to `%s'",
                  action2str(action));

        filename = ctx->argv[i++];
        args.file = fopen(filename, "w");
        if (args.file == NULL)
            ERROR(ctx, EXIT_FAILURE, errno, "fopen: %s", filename);
        break;
    default:
        break;
    }

    for (size_t i = 0; i < ctx->backend_count; i++)
        count += _find(ctx, i, action, filter, sorts, sorts_count, &args,
                       exec_action);

    switch (action) {
    case ACT_COUNT:
        printf("%lu matching entries\n", count);
        break;
    case ACT_FLS:
    case ACT_FPRINT:
    case ACT_FPRINT0:
        if (fclose(args.file))
            ERROR(ctx, EXIT_FAILURE, errno, "fclose: %s", filename);
        break;
    default:
        break;
    }

    *arg_idx = i;
}

static struct rbh_filter *
parse_predicate(struct find_context *ctx, int *arg_idx)
{
    enum predicate predicate;
    struct rbh_filter *filter;
    int i = *arg_idx;

    predicate = str2predicate(ctx->argv[i]);

    if (i + 1 >= ctx->argc)
        ERROR(ctx, EX_USAGE, 0, "missing argument to `%s'", ctx->argv[i]);

    /* In the following block, functions should call ERROR() themselves rather
     * than returning.
     *
     * Errors are most likely fatal (not recoverable), and this allows for
     * precise and meaningul error messages.
     */
    switch (predicate) {
    case PRED_AMIN:
    case PRED_MMIN:
    case PRED_CMIN:
        filter = xmin2filter(predicate, ctx->argv[++i]);
        break;
    case PRED_ATIME:
    case PRED_MTIME:
    case PRED_CTIME:
        filter = xtime2filter(predicate, ctx->argv[++i]);
        break;
    case PRED_NAME:
        filter = shell_regex2filter(predicate, ctx->argv[++i], 0);
        break;
    case PRED_INAME:
        filter = shell_regex2filter(predicate, ctx->argv[++i],
                                    RBH_RO_CASE_INSENSITIVE);
        break;
    case PRED_TYPE:
        filter = filetype2filter(ctx->argv[++i]);
        break;
    case PRED_SIZE:
        filter = filesize2filter(ctx->argv[++i]);
        break;
    case PRED_PERM:
        filter = mode2filter(ctx->argv[++i]);
        break;
    case PRED_XATTR:
        filter = xattr2filter(ctx->argv[++i]);
        break;
    default:
        ERROR(ctx, EXIT_FAILURE, ENOSYS, "%s", ctx->argv[i]);
        /* clang: -Wsometimes-unitialized: `filter` */
        __builtin_unreachable();
    }
    assert(filter != NULL);

    *arg_idx = i;
    return filter;
}

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

    filter = parse_expression(&ctx, &index, NULL, &sorts, &sorts_count,
                              parse_predicate, find);
    if (index != ctx.argc)
        ERROR(&ctx, EX_USAGE, 0, "you have too many ')', idx = %d, argc = %d", index, ctx.argc);

    if (!ctx.action_done)
        find(&ctx, ACT_PRINT, &index, filter, sorts, sorts_count);
    free(filter);

    return EXIT_SUCCESS;
}
