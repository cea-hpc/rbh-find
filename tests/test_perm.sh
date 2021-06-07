#!/bin/bash

if ! command -v rbh-sync &> /dev/null; then
    echo "This test requires rbh-sync to be installed"
    exit 1
fi

rbh_find=./rbh-find

function error
{
    echo "$*"
    exit 1
}

function test_octal
{
    local dir="/tmp/test_octal"
    local URI="rbh:mongo:test_octal"

    local perms=(001 002 003 004 005 006 007
                 010 020 030 040 050 060 070
                 100 200 300 400 500 600 700
                 111 222 333 444 555 666 777)
    local perm_minus=(8 8 4 8 4 4 2
                      8 8 4 8 4 4 2
                      8 8 4 8 4 4 2
                      4 4 2 4 2 2 1)
    local perm_slash=(8  8 12  8 12 12 14
                      8  8 12  8 12 12 14
                      8  8 12  8 12 12 14
                      16 16 24 16 24 24 28)

    mkdir -p $dir
    for perm in ${perms[*]}; do
        touch "$dir/file.$perm"
        chmod $perm "$dir/file.$perm"
    done

    rbh-sync rbh:posix:$dir $URI

    for ((i = 0; i < ${#perms[*]}; i++)); do
        local num=$($rbh_find $URI -perm ${perms[i]} | wc -l)

        (( $num == 1 )) ||
            error "$rbh_find -perm ${perms[i]}: $num != 1"

        num=$($rbh_find $URI -perm -${perms[i]} -type f | wc -l)
        (( $num == ${perm_minus[i]} )) ||
            error "$rbh_find -perm -${perms[i]}: $num != ${perm_minus[i]}"

        num=$($rbh_find $URI -perm /${perms[i]} -type f | wc -l)
        (( $num == ${perm_slash[i]} )) ||
            error "$rbh_find -perm /${perms[i]}: $num != ${perm_slash[i]}"
    done
}

function test_symbolic
{
    local dir="/tmp/test_symbolic"
    local URI="rbh:mongo:test_symbolic"
    # p is the permission of write and execute for user, group and other
    # without the umask. It is used to test +wx.
    local p=$(printf "%o" "$((0333 & ~$(umask)))")
    local perms=(1000 000 2000 4000 $p 644 111 110 100 004)
    local symbolic=(+t /+t a+t u+t g+t o+t g+s u+s o+s +s o+sr
                    o=r,ug+o,u+w u+ g+ o+ a+ ugo+
                    u- g- o- a- ugo- u= g= o= a= ugo=
                    o=r,ug+o,u+w u=r,a+u,u+w
                    g=r,ugo=g,u+w u+x,+X +X
                    u+x,u+X u+X u+x,g+X o+r,+X
                    u+x,go+X +wx +rwx)

    mkdir -p $dir
    for perm in ${perms[*]}; do
        touch "$dir/file.$perm"
        chmod $perm "$dir/file.$perm"
    done

    rbh-sync rbh:posix:$dir $URI

    for (( i = 0; i < ${#symbolic[*]}; i++ )); do
        local num=$($rbh_find $URI -perm ${symbolic[i]} | wc -l)

        (( $num == 1 )) ||
            error "$rbh_find $URI -perm ${symbolic[i]}: $num != 1"
    done
}

function test_error_perm
{
    local dir="/tmp/test_error_perm"
    local URI="rbh:mongo:test_error_perm"
    local tests="17777 787 789 abcd ug=uu ug=a ug=gu uo=ou urw u+xg+x a=r,u+x,"

    mkdir -p $dir
    rbh-sync rbh:posix:$dir $URI

    for err in $tests; do
        if $rbh_find $URI -perm $err 2>/dev/null; then
            error "$rbh_find -perm $err: parsing should have failed"
        fi
    done
}

function cleanup
{
    rm -rf /tmp/test_octal
    rm -rf /tmp/test_symbolic
    rm -rf /tmp/test_error_perm
}

trap ERR EXIT cleanup

test_octal
test_symbolic
test_error_perm
