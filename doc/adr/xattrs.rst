.. This file is part of the RobinHood Library
   Copyright (C) 2021 Commissariat a l'energie atomique et aux energies
                      alternatives

   SPDX-License-Identifier: LGPL-3.0-or-later

###############
rbh-find -xattr
###############

NOTE: This document is an Architecture Decision Record (ADR).

The goal of this document is to describe the semantics of the
``rbh-find -xattr`` command to filter backend entries based on their extended
attributes.

What is an extended attribute (xattr)?
======================================

An extended attribute is a file metadata which allow to store additional
information. Those pieces of information can be both set by the user, or the
system. For instance, Lustre file system defines a ``lustre.lov`` xattr to
manage the file layout.

An extended attribute is a key-value pair. The attribute name (key) is a
null-terminated string which is always specified in the fully qualified
``namespace.attribute`` form.

The value field consists in a chunk of textual or binary data and can represent
any kind of data type, without specifying it. Only the size of the chunk is
specified.

================ ===========================================
Type             Example
================ ===========================================
string           ``trusted.toto="abc"``
number           ``trusted.toto="42.13"``
datetime (epoch) ``trusted.toto="1637754583"``
binary           ``trusted.toto="gQAAAMwPBQEAAAAArmKrOw=="``
set              ``trusted.toto="[abc, def, ghi]"``
map              ``trusted.toto="{3: abc, 42: def}"``
================ ===========================================

The format used here to represent complex data structures, such as sets and
maps, is the folded version of YAML representations of respectively lists and
dictionaries.

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

Why do we need to filter on xattrs?
===================================

Data placement policies may require additional information to select the files
to relocate. In our use cases, we can think of:

1. filtering on user-driven hint to specify a file will be modified (or not),
   accessed (or not) or archived (or not);
2. filtering on Lustre OSTs;
3. filtering on predicted file lifetime.

Those use cases will drive which kind of filter we need for extended attributes:

1. Will the file be accessed? ::

    rbh-find on files where user.hsm_hint xattr includes will-access

2. Is the file located on OST #123 to #127? ::

    rbh-find on files where user.lustre_ost xattr is between 123 and 127

3. Does the file lifetime expired? ::

    rbh-find on files where user.lifetime is lesser than current datetime

Command line parsing
====================

Parsing xattr names
-------------------

This part is easy to parse, especially when checking xattr existence: the
name is a null-terminated string.

Parsing xattr filters
---------------------

We thought of two ways:

1. using unicode symbol operators::

    rbh-find rbh:mongo:demo -xattr trusted.toto=value
    rbh-find rbh:mongo:demo -xattr trusted.toto!=value
    rbh-find rbh:mongo:demo -xattr "trusted.toto<value"
    rbh-find rbh:mongo:demo -xattr trusted.toto∈value

2. using literal operators::

    rbh-find rbh:mongo:demo -xattr trusted.toto -eq value
    rbh-find rbh:mongo:demo -xattr trusted.toto -ne value
    rbh-find rbh:mongo:demo -xattr trusted.toto -lt value
    rbh-find rbh:mongo:demo -xattr trusted.toto -inc value

The first option is shortier and easier to parse than the second one. But there
is a huge drawback: we can not correctly parse names which contain operator
characters.

We will still first implement the first option, warning that attribute names
which contain unsupported characters will not be considered. The second option
can be implemented later.

An alternative was thought using escaped quotes to encapsulate the attribute
name like ``trusted.\"to=to\"``, but this can only fit if there is no quotes in
the name, which can not be ensured.

Parsing xattr values
--------------------

An attribute value is a chunk of data, which can be textual or binary. Instead
of strings, binary data can not be given through the command line. We can think
of two ways of doing:

1. getting encoded binary data from the command line;
2. getting binary data from a pipe or file;
3. passing binary data through an API.

The third option is a feature wihch is planned later. The second one is a bit
more complicated: we need to differentiate strings to binary data, which can
be done using different filters (``-xattr`` and ``-sxattr`` for example).

The first option is practicable if we offer a way for the user to encode their
binary data and then pass them to the command line.

Command lines examples
======================

Filter on extended attribute existence
--------------------------------------

The entry is selected only if an attribute with the given name exists::

    rbh-find rbh:mongo:demo -xattr trusted.toto

Coupled with the ``-not`` operator, we can also filter entries which do not have
the attribute::

    rbh-find rbh:mongo:demo -not -xattr trusted.toto

Filter on extended attribute comparison
---------------------------------------

The entry is selected only if the value of the target attribute exists and
verifies the given condition::

    rbh-find rbh:mongo:demo -xattr trusted.toto=abc
    rbh-find rbh:mongo:demo -not -xattr trusted.toto="abc def"

It may be useful to support other comparison operators: `<`, `>`, `<=`
and `>=`::

    rbh-find rbh:mongo:demo -xattr "trusted.toto<42"

Note that this syntax may not work for attribute names containing one of those
symbols. If one may be used in the attribute names, the following syntax must
be used instead::

    rbh-find rbh:mongo:demo -xattr trusted.to=to -eq abc
    rbh-find rbh:mongo:demo -xattr "trusted.to<to" -lt 42

================ ============== =============
Comparison       Short operator Long operator
================ ============== =============
Equality         `=`            ``-eq``
Lesser than      `<`            ``-lt``
Lesser or Equal  `<=`           ``-le``
Greater than     `>`            ``-gt``
Greater or Equal `>=`           ``-ge``
================ ============== =============

Advanced filter on extended attributes
-------------------------------------

We currently have two different cases that still need to be discussed,
where the solution may not be so trivial:

* set/map inclusion: check a value is included in a set/map attribute::

    rbh-find rbh:mongo:demo -xattr trusted.toto -inc 42
    rbh-find rbh:mongo:demo -xattr trusted.toto -inc titi=42

* multiple values: select the entry if its attribute value is in a given set::

    rbh-find rbh:mongo:demo -xattr trusted.toto -in [2,5,236-426]

Answer to the use cases
-----------------------

1. Will the file be accessed? ::

    rbh-find rbh:mongo:demo -xattr user.hsm_hint=*will-access*

2. Is the file located on OST #123 to #127? ::

    rbh-find rbh:mongo:demo -xattr user.lustre_ost -in [123-127]

3. Does the file lifetime expired? ::

    rbh-find rbh:mongo:demo -xattr "user.lifetime<"$(date +%s)

Going further
=============

Dealing with the backend filter generation, there is still a main point to
discuss: all extended attributes are stored as binary data in the Mongo backend,
and Mongo does not seem to offer a way to convert binary data on the fly to make
typed comparisons. However, we can operate string-like equality and
lesser/greater than comparisons, by first checking the chunk size, then compare
byte by byte.

Even if the lifetime use case can fit in what we are capable of, the two others
do not.

We thought of a solution that affect rbh-sync and rbh-enrich: we may store some
known extended attributes with other types than binary data. Those types can be
indicated through the command line, during synchronization or enrichment, or
using a mapping file:

1. Through the command line. ::

    rbh-sync rbh:posix:/path/to/fs rbh:mongo:demo \
             -xattr-type user.hsm_hint string \
             -xattr-type user.lustre_ost integer \
             -xattr-type user.lifetime datetime

2. Through a mapping file. ::

    $ cat mapping.yaml
    ---
    xattr-mapping:
      - user.hsm_hint: string
      - user.lustre_ost: integer
      - user.lifetime: datetime
    ...
    $ rbh-sync rbh:posix:/path/to/fs rbh:mongo:demo -xattr-mapping mapping.yaml

The first option seems to be more rbhv4-like (as seen with include/exclude
options of rbh-sync), but may lead to write really long commands, be more
error-prone. This choice is still in discussion, and we are still seeking for
other solutions.
