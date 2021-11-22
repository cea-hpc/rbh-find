#!/usr/bin/env bash

set -e

if ! command -v rbh-sync &> /dev/null; then
    echo "This test requires rbh-sync to be installed"
    exit 1
fi

stat ./rbh-find && rbh_find=./rbh-find || rbh_find=rbh-find

function error
{
    echo "$*"
    exit 1
}

function cleanup
{
    rm -rf $1
    mongo --quiet $2 --eval "db.dropDatabase()"
}

function verify_output
{
    local output="$1"
    local n_lines=$(($# - 1))

    local output_lines=$(echo -n "$output" | grep -c '^')
    if [ $output_lines -ne $n_lines ]; then
        error "wrong number of lines, expected '$n_lines' lines, found" \
              "'$output_lines' lines in '$output'"
    fi

    shift 1

    while ! [ -z $1 ]; do
        local test_file="$1"
        if ! echo "$output" | grep $test_file; then
            error "file '$test_file' was not listed in '$output'"
        fi
        shift
    done
}

function test_size
{
    local path="$1"
    local posix_URI="$2"
    local mongo_URI="$3"
    local find_mongo_URI="$4"

    truncate -s 1M "$path/fileA"
    truncate -s 1k "$path/fileB"
    truncate -s 1k "$path/fileC" && echo "hello world!" >> "$path/fileC"

    rbh-sync "$posix_URI" "$mongo_URI"

    local output="$($rbh_find "$find_mongo_URI" -size 1M)"
    verify_output "$output" "/" "fileA" "fileB" "fileC"

    local output="$($rbh_find "$find_mongo_URI" -size +1k)"
    verify_output "$output" "fileA" "fileC"

    local output="$($rbh_find "$find_mongo_URI" -size +1k -size -1M)"
    verify_output "$output"
}

function test_size_simple
{
    local dir=$(mktemp --dir)
    local random_string="${dir##*.}"

    trap "cleanup $dir $random_string" EXIT

    test_size "$dir" "rbh:posix:$dir" "rbh:mongo:$random_string" \
              "rbh:mongo:$random_string"

    cleanup $dir $random_string
}

function test_size_with_branch
{
    local dir=$(mktemp --dir)
    local random_string="${dir##*.}"

    mkdir -p "$dir/dir"

    trap "cleanup $dir $random_string" EXIT

    test_size "$dir/dir" "rbh:posix:$dir" "rbh:mongo:$random_string" \
              "rbh:mongo:$random_string#dir"

    cleanup $dir $random_string
}

test_size_simple
test_size_with_branch

trap - EXIT
