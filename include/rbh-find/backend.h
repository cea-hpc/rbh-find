/* This file is part of rbh-find
 * Copyright (C) 2022 Commissariat a l'energie atomique et aux energies
 *                    alternatives
 *
 * SPDX-License-Identifer: LGPL-3.0-or-later
 */

#ifndef RBH_FIND_CONTEXT_H
#define RBH_FIND_CONTEXT_H

#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>

#include <robinhood.h>
#include <robinhood/utils.h>

#include "rbh-find/actions.h"
#include "rbh-find/filters.h"
#include "rbh-find/parser.h"

/**
 * Find's library context
 */
struct find_context {
    /** The backends to go through */
    size_t backend_count;
    struct rbh_backend **backends;
    /** The command-line arguments to parse */
    int argc;
    char **argv;
    /** If an action was already executed in this specific execution */
    bool action_done;
};

union action_arguments {
    /* ACT_FLS, ACT_FPRINT, ACT_FPRINT0, ACT_FPRINTF */
    FILE *file;
};

void
exit_backends(struct find_context *ctx);

#define ERROR(_ctx, _exit_code, _error_code, _fmt, ...)         \
do {                                                            \
    exit_backends((_ctx));                                      \
    error((_exit_code), (_error_code), (_fmt), ##__VA_ARGS__);  \
} while (0)

#define ERROR_AT_LINE(_ctx, _exit_code, _error_code, _filename, _fileline,     \
                      _fmt, ...)                                               \
do {                                                                           \
    exit_backends((_ctx));                                                     \
    error_at_line((_exit_code), (_error_code), (_filename), (_fileline),       \
                  (_fmt), ##__VA_ARGS__);                                      \
} while (0)

size_t
_find(struct find_context *ctx, int backend_index, enum action action,
      const struct rbh_filter *filter, const struct rbh_filter_sort *sorts,
      size_t sorts_count, const union action_arguments *args,
      int (*action_callback)(struct find_context*, enum action,
                             struct rbh_fsentry*,
                             const union action_arguments*));

struct rbh_filter *
parse_expression(struct find_context *ctx, int *arg_idx,
                 const struct rbh_filter *_filter,
                 struct rbh_filter_sort **sorts, size_t *sorts_count,
                 struct rbh_filter* (*predicate_callback)(struct find_context*,
                                                          int *),
                 void (*find_callback)(struct find_context*, enum action, int*,
                                       const struct rbh_filter*,
                                       const struct rbh_filter_sort*,
                                       size_t ));

#endif
