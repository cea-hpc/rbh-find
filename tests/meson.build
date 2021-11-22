# This file is part of the RobinHood Library
# Copyright (C) 2021 Commissariat a l'energie atomique et aux energies
#                    alternatives
#
# SPDX-License-Identifer: LGPL-3.0-or-later

check = dependency('check', version: '>=0.9.9', required: false)

if not check.found()
    message('check is missing: skipping testing')
    subdir_done()
endif

integration_tests = ['test_perm', 'test_size']

foreach t: integration_tests
    e = find_program(t + '.bash')
    test(t, e)
endforeach
