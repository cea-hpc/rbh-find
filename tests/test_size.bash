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
    diff -y - <(printf '%s\n' "$@")
}

################################################################################
#                                    TESTS                                     #
################################################################################

test_equal_1K()
{
    touch "empty"
    truncate --size 1K "1K"
    truncate --size 1025 "1K+1"
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb" -size 1k | sort |
        difflines "/" "/1K"
}

test_plus_1K()
{
    touch "empty"
    truncate --size 1K "1K"
    truncate --size 1025 "1K+1"
    truncate --size 1M "1M"
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb" -size +1k | sort |
        difflines "/1K+1" "/1M"
}

test_plus_1K_minus_1M()
{
    touch "empty"
    truncate --size 1K "1K"
    truncate --size 1025 "1K+1"
    truncate --size 1M "1M"
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    local output=$(rbh_find "rbh:mongo:$testdb" -size +1k -size -1M)
    if [ ! -z $output ]; then
        echo "output was not empty, got '$output'"
        return 1
    fi
}

test_equal_1M()
{
    touch "empty"
    truncate --size 1M "1M"
    truncate --size $((1024 * 1024 + 1)) "1M+1"
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb" -size 1M | sort |
        difflines "/" "/1M"
}

test_minus_1M()
{
    touch "empty"
    truncate --size 1M "1M"
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb" -size -1M | sort |
        difflines "/empty"
}

test_plus_3M()
{
    touch "empty"
    truncate --size 4M "4M"
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb" -size +3M | sort |
        difflines "/4M"
}

test_plus_1M_minus_2G()
{
    touch "empty"
    truncate --size 4M "4M"
    truncate --size 1M "1.xM"
    echo "hello world!" >> 1.xM
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb" -size +1M -size -2G | sort |
        difflines "/1.xM" "/4M"
}

test_branch_equal_1K()
{
    mkdir "dir"
    touch "dir/empty"
    truncate --size 1K "dir/1K"
    truncate --size 1025 "dir/1K+1"
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb#dir" -size 1k | sort |
        difflines "/dir" "/dir/1K"
}

test_branch_plus_1K()
{
    mkdir "dir"
    touch "dir/empty"
    truncate --size 1K "dir/1K"
    truncate --size 1025 "dir/1K+1"
    truncate --size 1M "dir/1M"
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb#dir" -size +1k | sort |
        difflines "/dir/1K+1" "/dir/1M"
}

test_branch_plus_1K_minus_1M()
{
    mkdir "dir"
    touch "dir/empty"
    truncate --size 1K "dir/1K"
    truncate --size 1025 "dir/1K+1"
    truncate --size 1M "dir/1M"
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    local output=$(rbh_find "rbh:mongo:$testdb#dir" -size +1k -size -1M)
    if [ ! -z $output ]; then
        echo "output was not empty, got '$output'"
        return 1
    fi
}

test_branch_equal_1M()
{
    mkdir "dir"
    touch "dir/empty"
    truncate --size 1M "dir/1M"
    truncate --size $((1024 * 1024 + 1)) "dir/1M+1"
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb#dir" -size 1M | sort |
        difflines "/dir" "/dir/1M"
}

test_branch_minus_1M()
{
    mkdir "dir"
    touch "dir/empty"
    truncate --size 1M "dir/1M"
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb#dir" -size -1M | sort |
        difflines "/dir/empty"
}

test_branch_plus_3M()
{
    mkdir "dir"
    touch "dir/empty"
    truncate --size 4M "dir/4M"
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb#dir" -size +3M | sort |
        difflines "/dir/4M"
}

test_branch_plus_1M_minus_2G()
{
    mkdir "dir"
    touch "dir/empty"
    truncate --size 4M "dir/4M"
    truncate --size 1M "dir/1.xM"
    echo "hello world!" >> dir/1.xM
    rbh-sync "rbh:posix:." "rbh:mongo:$testdb"

    rbh_find "rbh:mongo:$testdb#dir" -size +1M -size -2G | sort |
        difflines "/dir/1.xM" "/dir/4M"
}

################################################################################
#                                     MAIN                                     #
################################################################################

declare -a tests=(test_equal_1K test_plus_1K test_plus_1K_minus_1M
                  test_equal_1M test_minus_1M test_plus_3M
		  test_plus_1M_minus_2G test_branch_equal_1K
		  test_branch_plus_1K test_branch_plus_1K_minus_1M
		  test_branch_equal_1M test_branch_minus_1M
		  test_branch_plus_3M test_branch_plus_1M_minus_2G)

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
