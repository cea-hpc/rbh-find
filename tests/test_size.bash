#!/usr/bin/env bash

# This file is part of the RobinHood Library
# Copyright (C) 2021 Commissariat a l'energie atomique et aux energies
#                    alternatives
#
# SPDX-License-Identifer: LGPL-3.0-or-later

set -e

if ! command -v rbh-sync &> /dev/null; then
    echo "This test requires rbh-sync to be installed"
    exit 1
fi

__rbh_find="$(PATH="$PWD:$PATH" which rbh-find)"
function rbh_find()
{
    "$__rbh_find" "$@"
}

function error
{
    echo "$*"
    exit 1
}

function cleanup
{
    rm -rf "$1"
    mongo --quiet "$2" --eval "db.dropDatabase()"
}

function setup
{
    local root="$1"
    local path="$2"
    local dest="$3"

    truncate -s 1M "$path/fileA"
    truncate -s 1k "$path/fileB"
    truncate -s 1k "$path/fileC" && echo "hello world!" >> "$path/fileC"
    touch "$path/fileD"

    rbh-sync "rbh:posix:$root" "rbh:mongo:$dest"
}

function difflines
{
    diff -y - <(printf '%s\n' "$@")
}

function test_size
{
    local find_mongo_URI="$1"
    local root="$2"
    local prefix="$3"

    local expected
    local output

    expected="$(printf \
        "$root\n${prefix}/fileA\n${prefix}/fileB\n${prefix}/fileC")"
    rbh_find "$find_mongo_URI" -size 1M | sort | \
        difflines "$expected" || error "output was different than expected"

    expected="$(printf "${prefix}/fileA\n${prefix}/fileC")"
    rbh_find "$find_mongo_URI" -size +1k | sort | \
        difflines "$expected" || error "output was different than expected"

    output="$(rbh_find "$find_mongo_URI" -size +1k -size -1M)"
    if [ -n "${output}" ]; then
        error "output was not empty"
    fi

    expected="$(printf "${prefix}/fileD")"
    rbh_find "$find_mongo_URI" -size -1M | sort | \
        difflines "$expected" || error "output was different than expected"
}

function test_size_simple
{
    local dir="$(mktemp --dir)"
    local random_string="${dir##*.}"

    trap 'cleanup $dir $random_string' EXIT

    setup "$dir" "$dir" "$random_string"

    test_size "rbh:mongo:$random_string" "/"

    cleanup "$dir" "$random_string"
}

function test_size_with_branch
{
    local dir="$(mktemp --dir)"
    local random_string="${dir##*.}"

    mkdir -p "$dir/dir"

    trap 'cleanup $dir $random_string' EXIT

    setup "$dir" "$dir/dir" "$random_string"

    test_size "rbh:mongo:$random_string#dir" "/dir" "/dir"

    cleanup "$dir" "$random_string"
}

test_size_simple
test_size_with_branch
