/* This file is part of rbh-find
 * Copyright (C) 2022 Commissariat a l'energie atomique et aux energies
 *                    alternatives
 *
 * SPDX-License-Identifer: LGPL-3.0-or-later
 */

#ifndef RBH_FIND_CORE_H
#define RBH_FIND_CORE_H

#include <error.h>
#include <stdio.h>
#include <stdlib.h>

#include <robinhood.h>

#include "rbh-find/actions.h"
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
    FILE *action_file;

    /**
     * Callback to prepare an action's execution
     *
     * @param ctx      find's context for this execution
     * @param index    the command line index of the action token
     * @param action   the type of action to prepare for
     *
     * @return         int corresponding to how many command line tokens were
     *                 consumed
     */
    int (*pre_action_callback)(struct find_context *ctx, const int index,
                               const enum action action);

    /**
     * Callback for executing an action
     *
     * @param ctx      find's context for this execution
     * @param action   the type of action to execute
     * @param fsentry  the fsentry to act on
     *
     * @return         1 if the action is ACT_COUNT, 0 otherwise.
     */
    int (*exec_action_callback)(struct find_context *ctx, enum action action,
                                struct rbh_fsentry *fsentry);

    /**
     * Callback to finish an action's execution
     *
     * @param ctx      find's context for this execution
     * @param index    the command line index of the token after the action
     * @param action   the type of action to prepare for
     * @param count    number of entries found with this action
     */
    void (*post_action_callback)(struct find_context *ctx, const int index,
                                 const enum action action, const size_t count);
};

/**
 * Destroy and free the backends of a `struct find_context`
 *
 * @param ctx      find's context for this execution
 */
void
ctx_finish(struct find_context *ctx);

#define ERROR(_ctx, _exit_code, _error_code, _fmt, ...)         \
do {                                                            \
    ctx_finish((_ctx));                                         \
    error((_exit_code), (_error_code), (_fmt), ##__VA_ARGS__);  \
} while (0)

#define ERROR_AT_LINE(_ctx, _exit_code, _error_code, _fmt, ...)                \
do {                                                                           \
    ctx_finish((_ctx));                                                        \
    error_at_line((_exit_code), (_error_code), __FILE__, __LINE__, (_fmt),     \
                  ##__VA_ARGS__);                                              \
} while (0)

/**
 * Core pre_action function, see `pre_action_callback` in `struct
 * find_context` for more information.
 *
 * Called by rbh-find and implement GNU-like behaviour.
 */
int
core_pre_action(struct find_context *ctx, const int index,
                const enum action action);

/**
 * Core exec_action function, see `exec_action_callback` in `struct
 * find_context` for more information.
 *
 * Called by rbh-find and implement GNU-like behaviour.
 */
int
core_exec_action(struct find_context *ctx, enum action action,
                 struct rbh_fsentry *fsentry);

/**
 * Core post_action function, see `post_action_callback` in `struct
 * find_context` for more information.
 *
 * Called by rbh-find and implement GNU-like behaviour.
 */
void
core_post_action(struct find_context *ctx, const int index,
                 const enum action action, const size_t count);

#endif
