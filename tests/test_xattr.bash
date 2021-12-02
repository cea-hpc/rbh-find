#!/usr/bin/env bash

# This file is part of the RobinHood Library
# Copyright (C) 2021 Commissariat a l'energie atomique et aux energies
#                    alternatives
#
# SPDX-License-Identifer: LGPL-3.0-or-later

set -e

if ! command -v rbh-sync &> /dev/null; then
    echo "This test requires rbh-sync to be installed" >&2
    exit 1
fi

################################################################################
#                                  UTILITIES                                   #
################################################################################

SUITE=${BASH_SOURCE##*/}
SUITE=${SUITE%.*}

__rbh_find=$(PATH="$PWD:$PATH" which rbh-find)
rbh_find()
{
    "$__rbh_find" "$@"
}

__mongo=$(which mongosh || which mongo)
mongo()
{
    "$__mongo" "$@"
}

setup()
{
    # Create a test directory and `cd` into it
    testdir=$SUITE-$test
    mkdir "$testdir"
    cd "$testdir"

    # "Create" a test database
    testdb=$SUITE-$test
}

teardown()
{
    mongo --quiet "$testdb" --eval "db.dropDatabase()" >/dev/null
    rm -rf "$testdir"
}

difflines()
{
    diff -y - <([ $# -eq 0 ] && printf '' || printf '%s\n' "$@")
}

################################################################################
#                                    TESTS                                     #
################################################################################

test_xattr_exists_no_arg()
{
    touch "a"
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb" -xattr && \
        echo "Parsing should have failed" && return 1
    return 0
}

test_xattr_exists()
{
    touch "a"
    setfattr -n user.a -v b a
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb" -xattr user | sort |
        difflines "/a"
    rbh_find "rbh:mongo:$testdb" -xattr user.a | sort |
        difflines "/a"
}

test_xattr_not_exists()
{
    touch "a"
    setfattr -n user.a -v b a
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb" -xattr blob | sort | difflines
    rbh_find "rbh:mongo:$testdb" -xattr user.b | sort | difflines
}

test_not_xattr_exists()
{
    touch "a"
    setfattr -n user.a -v b a
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb" -not -xattr user | sort |
        difflines "/"
    rbh_find "rbh:mongo:$testdb" -not -xattr user.a | sort |
        difflines "/"
    rbh_find "rbh:mongo:$testdb" -not -xattr user.b | sort |
        difflines "/" "/a"
    rbh_find "rbh:mongo:$testdb" -not -xattr blob | sort |
        difflines "/" "/a"
}

test_not_not_xattr_exists()
{
    touch "a"
    setfattr -n user.a -v b a
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb" -not -not -xattr user | sort |
        difflines "/a"
    rbh_find "rbh:mongo:$testdb" -not -not -xattr user.a | sort |
        difflines "/a"
}

################################################################################
#                                     MAIN                                     #
################################################################################

declare -a tests=(test_xattr_exists_no_arg test_xattr_exists
                  test_xattr_not_exists test_not_xattr_exists
                  test_not_not_xattr_exists)

tmpdir=$(mktemp --directory)
trap -- "rm -rf '$tmpdir'" EXIT
cd "$tmpdir"

for test in "${tests[@]}"; do
    (
    trap -- "teardown" EXIT
    setup

    ("$test") && echo "$test: ✔" || echo "$test: ✖"
    )
done
