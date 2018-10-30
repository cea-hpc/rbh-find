/* This file is part of the RobinHood Library
 * Copyright (C) 2019 Commissariat a l'energie atomique et aux energies
 *                    alternatives
 *
 * SPDX-License-Identifer: LGPL-3.0-or-later
 *
 * author: Quentin Bouget <quentin.bouget@cea.fr>
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "filters.h"
#include "utils.h"

#include <errno.h>
#include <error.h>
#include <limits.h>
#include <stdlib.h>

static const enum rbh_filter_field predicate2filter_field[] = {
    [PRED_AMIN]     = RBH_FF_ATIME,
    [PRED_MMIN]     = RBH_FF_MTIME,
    [PRED_CMIN]     = RBH_FF_CTIME,
    [PRED_INAME]    = RBH_FF_NAME,
    [PRED_NAME]     = RBH_FF_NAME,
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

    filter = rbh_filter_compare_regex_new(predicate2filter_field[predicate],
                                          pcre, regex_options);
    if (filter == NULL)
        error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__ - 2,
                      "building a regex filter for %s", pcre);
    free(pcre);
    return filter;
}

static const char *
num2operator(const char *num, enum rbh_filter_operator *op)
{
    switch (num[0]) {
    case '+':
        *op = RBH_FOP_LOWER_THAN;
        return &num[1];
    case '-':
        *op = RBH_FOP_GREATER_THAN;
        return &num[1];
    default:
        *op = RBH_FOP_EQUAL;
        return num;
    }
}

static struct rbh_filter *
timedelta2filter(enum predicate predicate, enum time_unit unit,
                 const char *_timedelta)
{
    enum rbh_filter_operator op;
    const char *timedelta;
    struct rbh_filter *filter;
    unsigned long delta; /* in seconds */
    time_t then, now;
    int old_errno;

    /* Parse the leading '+', '-', or lack thereof */
    timedelta = num2operator(_timedelta, &op);

    /* Convert the time string to a number of seconds */
    old_errno = errno;
    errno = 0;
    delta = str2seconds(unit, timedelta);
    if ((errno == ERANGE && delta == ULONG_MAX) || (errno != 0 && delta == 0))
        error(EXIT_FAILURE, 0, "invalid argument `%s' to `%s'", _timedelta,
              predicate2str(predicate));
    errno = old_errno;

    /* Compute `then' */
    now = time(NULL);
    if (now < 0)
        error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__, "time");
    then = now < delta ? 0 : now - delta;

    /* FIXME */
    filter = rbh_filter_compare_int64_new(op, predicate2filter_field[predicate],
                                          then);
    if (filter == NULL)
        error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__,
                      "filter_compare_time");

    return filter;
}

struct rbh_filter *
xmin2filter(enum predicate predicate, const char *minutes)
{
    return timedelta2filter(predicate, TU_MINUTE, minutes);
}
