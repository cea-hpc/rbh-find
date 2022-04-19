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
