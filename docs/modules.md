# Modules

A **module** is the minimum translation unit of Berry code.  All definitions in
a module share the same global namespace.  A module may be composed of a single,
standalone file or many files in the same directory.

**Table of Contents**:

- [Module Construction](#mod-cons)
- [File Naming](#naming)
- [Import Resolution](#import-res)
- [Import Statements](#import-stmts)
- [Initialization Functions](#init-funcs)

## <a name="mod-cons"/> Module Construction

Module names and paths must correspond to their enclosing directory structure.
The module a particular file belongs to is communicated through an (optional)
**module declaration**:

```berry
module mod_name;
```

If a file doesn't have a module declaration, it becomes a single-file module where
its name is determined directly from its file name (see [File Naming](#naming)).

If a file does have a module declaration, then that declaration can take the
form of either a single name (indicating a module at the top of its hierarchy)
or a module path (which places the module as a submodule in a module hierarchy).
The last name in a module's path is called its **leaf name** and the rest of the
path is called its **stem**.

A module's leaf name must either correspond to the name of the file or the name
of the enclosing directory.  The former indicates that the module is a
single-file module, and the latter indicates that the module is a part of a
multi-file module spanning the current directory.

The stem of the module path, if present, must correspond directly to the
directory tree enclosing the current file.  For example, consider a file with
the module declaration:

```berry
module a.b.c;
```

If this file is to be a single-file module, then it must have the path
`a/b/c.bry` If this file is to be a multi-file (directory) module, then it must
be enclosed in the path `a/b/c`.  In terms of naming requirements, `a`, `b`, and
`c` must be meet the requirements in [File Naming](#naming).  Importantly, for
directory modules, file names do not need to satisfy these requirements
(although, we recommend that they should).

Note that under this scheme, it is possible for single-file modules and
multi-file modules to coexist in the same directory.  For example, if we had the
directory layout:

    foo/
        a.bry
        b.bry
        c.bry
        d.bry
        bar/
            e.bry

It would be perfectly legal for `a.bry`, `b.bry`, and `c.bry` to all be apart of
the module `foo` and for `d.bry` to be a standalone module named either `d` or
`foo.d`.  Furthermore, the file `e.bry` could have any of the following module
paths:

    e
    bar.e
    foo.bar.e
    bar
    foo.bar

## <a name="naming"/> File Naming

Since module names are inferred from the names of files and directories, any
file or directory which is intended to compose part of a module path must have a
valid name.  For enforce uniformity and fast lookups, files and directories
composing module paths *must* have names which are valid Berry identifiers.
That is, they must match the regex: `[a-zA-Z_][a-zA-Z0-9_]*`.  

## <a name="import-res"/> Import Resolution

Import resolution happens via two primary mechanisms:

1. Relative Search
2. Absolute Search

**Relative search** is the default resolution mechanism.  It finds modules by
using the local directory structure and module hierarchy.  The general approach
can be stated as: *search from the parent down*.  In essence, any module can
import any other module which is adjacent to or below its parent in the module
hierarchy. For instance, consider the modules:

    foo/
        a.bry [foo]
        b.bry [foo.b]
        c.bry [c]
        bar/
            d.bry [bar.d]
            e.bry [foo.bar.e]
            f.bry [f]
    baz/
        g.bry [baz.g]
        h.bry [h]

The module `foo` can import every other module including the nested modules `f`
and `h` as could the module `foo.b` since its parent is also `foo`.  But, the
module `c` can only import `foo`, `foo.b`, and the modules in `bar/`.  It cannot
see anything outside of `foo`. Finally, the module `f`, can only see the module
`bar.d` despite being a peer to `foo.bar.e` because the parent hierarchy of `e`
is `foo` which is one directory level above the root of the hierarchy for `f`.

We recommend that you do not organize your modules this way due to the weird
behavior shown above.  The idiomatic way to organize this hierarchy would be:

    foo/
        a.bry [foo]
        b.bry [foo.b]
        c.bry [foo.c]
        bar/
            d.bry [foo.bar.d]
            e.bry [foo.bar.e]
            f.bry [foo.bar.f]
    baz/
        g.bry [baz.g]
        h.bry [baz.h]

As a final note, modules can shadow each other meaning that if you have a
situation like:

    a.bry [a]
    b.bry [b]
    foo/
        b.bry [b]

The module `a` will only be able to import the module `b` that is outside of
`foo/`.  Again, we recommend against structuring your project's this way.

If relative search fails to locate a matching module, then **absolute search**
is invoked.  This system works based off of a series of **import paths** which
are either part of the compiler or specified explicitly.  All compilations
automatically have the import paths `mods/std` (standard library) and `mods/pub`
(global installed modules) added by default.  

Absolute search finds modules by simply exploring the top most level of the
import path to find any directories or files matching the desired module path.
Unlike relative search which works recursively starting from the module root,
absolute search does not explore recursively except in the case when the module
path has multiple components.

For example, the module path `io` is resolved absolutely by searching all import
paths for an directory named `io` or a file named `io.bry` *at the top level* of
the import path.  That is, if the import path is `mods/std`, then it will look
for a `mods/std/io.bry` and `mods/std/io` only.

Conversely, the module path `io.std` is resolved by looking for a file or
directory named `std` which is in the `io` subdirectory of the important path.
For example, if the import path is `mods/std`, then it will only check for
`mods/std/io/std` and `mods/std/io/std.bry`.

Absolute resolution is a more primitive fallback for imports to global
resources that don't require the level of sophisticated import semantics that
may be needed within a specific project.

## <a name="import-stmts"/> Import Statements

To import a module, one uses an **import statement**:

    import mod_name;

Imported modules are only visible within the *file* that imports them regardless
of whether that file is a single-file module or part of a multi-file module.
That is, all imported modules go in the file's local symbol table.

By default, an import will be visible under the leaf name of its module path.
For example, the import `fs.path` will be visible as `path` within its importing
file. 

Files can rename imports via an `as` clause to prevent conflicts or to provide
shorthand for commonly used modules with longer names.  For instance:

    import fs.path as fpath;  // Prevent conflicts

    import sequences as seq;  // Shorthand

Multiple imports can happen in one statement using parentheses:

    import ( io.std, fs.path );

One can rename imports using `as` clauses for each import.

    import (
        io.std,
        strings.conv as strconv,
        sequences as seq,
        threads,
        fs.path as fpath
    );

## <a name="init-funcs"/> Initialization Functions

All modules can define a special function named `init` that will run at the
start of any project that imports them before that project's `main` is called.

    func init() {
        // Run at startup!
    }

It is occasionally necessary to run a module's `init` function without
necessarily importing any of its functionality.  For this purpose, one can
import a module and rename it to `_` which indicates that the module's namespace
is not used.  

    import my_mod as _;

The contents of `my_mod` will not be visible in the importing file.
