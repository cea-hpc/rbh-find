/* This file is part of rbh-find
 * Copyright (C) 2022 Commissariat a l'energie atomique et aux energies
 *                    alternatives
 *
 * SPDX-License-Identifer: LGPL-3.0-or-later
 */

#ifndef RBH_FIND_CONTEXT_H
#define RBH_FIND_CONTEXT_H

#include <error.h>
#include <stdlib.h>

#include <robinhood.h>

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

void
ctx_finish(struct find_context *ctx);

#define ERROR(_ctx, _exit_code, _error_code, _fmt, ...)         \
do {                                                            \
    ctx_finish((_ctx));                                         \
    error((_exit_code), (_error_code), (_fmt), ##__VA_ARGS__);  \
} while (0)

#define ERROR_AT_LINE(_ctx, _exit_code, _error_code, _filename, _fileline,     \
                      _fmt, ...)                                               \
do {                                                                           \
    ctx_finish((_ctx));                                                        \
    error_at_line((_exit_code), (_error_code), (_filename), (_fileline),       \
                  (_fmt), ##__VA_ARGS__);                                      \
} while (0)

#endif
