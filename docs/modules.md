# Modules

A **module** is the minimum translation unit of Berry code.  All definitions in
a module share the same global namespace.  A module may be composed of a single,
standalone file or many files in the same directory.

## Module Construction

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

## Import Resolution

## Import Semantics