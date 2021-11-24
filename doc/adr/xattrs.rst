.. This file is part of the RobinHood Library
   Copyright (C) 2021 Commissariat a l'energie atomique et aux energies
                      alternatives

   SPDX-License-Identifier: LGPL-3.0-or-later

###############
rbh-find -xattr
###############

NOTE: This document is an Additional Development Request (ADR) and may not
reflect the actual implementation state of the described feature.

The goal of this document is to describe the utilization of the
``rbh-find -xattr`` command to filter backend entries following their extended
attributes.

What is an extended attribute?
==============================

An extended attribute is a key-value stored as a string:
``namespace.name="value"``

The value field can represent any kind of data type:

================ =======================================
Type             Example
================ =======================================
string           trusted.toto="abc"
number           trusted.toto="42.13"
datetime (epoch) trusted.toto="1637754583"
binary           trusted.toto="gQAAAMwPBQEAAAAArmKrOw=="
================ =======================================
set              trusted.toto="[abc, def, ghi]"
map              trusted.toto="{3: abc, 42: def}"
================ =======================================

It will be difficult, if not impossible, to assume a given format is used to
describe complex data structures, such as sets and maps, because they are not
all set by RobinHood. The one used here is the folded version of YAML
representations of respectively lists and dictionaries.

In case the extended attribute value is more complex, it can be represented in
a standard YAML format::

    trusted.toto="---
    tata: abc
    titi:
        - 12
        - 49
    tutu:
        - 3: abc
        - 42: def
    ..."

Filter on extended attribute existence
======================================

The entry is selected only if an attribute with the given name exists::

    rbh-find rbh:mongo:demo -xattr trusted.toto

Coupled with the `-not` operator, we can also filter entries which does not have
the attribute::

    rbh-find rbh:mongo:demo -not -xattr trusted.toto

Filter on extended attribute comparison
=======================================

The entry is selected only if the value of the target attribute verify the given
condition::

    rbh-find rbh:mongo:demo -xattr trusted.toto=abc
    rbh-find rbh:mongo:demo -not -xattr trusted.toto="abc def"

It may be useful to support other comparison operators: `<`, `>`, `<=`
and `>=`::

    rbh-find rbh:mongo:demo -xattr trusted.toto<42

Note that this syntax may not work for attribute names containing one of those
symbols. If one may be used in the attribute names, the following syntax must
be used instead::

    rbh-find rbh:mongo:demo -xattr trusted.to=to -eq abc
    rbh-find rbh:mongo:demo -xattr trusted.to<to -lt 42

================ ============== =============
Comparison       Short operator Long operator
================ ============== =============
Equality         `=`            `-eq`
Lesser than      `<`            `-lt`
Lesser or Equal  `<=`           `-le`
Greater than     `>`            `-gt`
Greater or Equal `>=`           `-ge`
================ ============== =============

Advanced filter on extended attributes
======================================

We currently have two different use cases that still need to be discussed,
where the solution may not be so trivial:

* set/map inclusion: check a value is included in a set/map attribute::

    rbh-find rbh:mongo:demo -xattr trusted.toto -inc 42
    rbh-find rbh:mongo:demo -xattr trusted.toto -inc titi=42

* multiple values: select the entry its attribute value is in a given set::

    rbh-find rbh:mongo:demo -xattr trusted.toto -in [2,5,236-426]
