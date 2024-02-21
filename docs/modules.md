# Modules

A **module** is the minimum translation unit of Berry code.  All definitions in
a module share the same global namespace.  A module may be composed of a single,
standalone file or many files in the same directory.

**Table of Contents**:

- [Import Resolution](#import-res)
- [Import Statements](#import-stmts)
- [Module Declarations](#mod-decl)
- [Initialization Functions](#init-funcs)

## <a name="import-res"/> Import Resolution

Imports are resolved via absolute paths relative to well-defined import
directories.  There is no notion of a "relative import" in Berry.

To refer to a module's location, one uses a **module path** which is
dot-separated sequence of identifiers:

    a.b.c

Module paths are always computed absolutely with respect to the import path. For
example, if the import path is `src/`, then the path `a.b.c` resolves to either
`src/a/b/c.bry` or `src/a/b/c` depending on whether `c` is a single-file or
multi-file module.

Since module names are inferred from the names of files and directories, any
file or directory which is intended to compose part of a module path must have a
valid name.  For enforce uniformity and fast lookups, files and directories
composing module paths *must* have names which are valid Berry identifiers. That
is, they must match the regex: `[a-zA-Z_][a-zA-Z0-9_]*`.  

All error reporting is done with respect to import paths. So, to report an error
in `src/a/b.bry`, the file will be pointed to as `[src] a/b.ry` where `src` is
the name of bottom-most directory of the import path.

If it happens that both a directory and a file match the specified name, then
the file is given precedence.  For example, in directory structure:

    src/
        a.bry
        b.bry
        b/
            c.bry
            d.bry

The module path `a.b` will always resolve first to `b.bry`.  If `b.bry` is part
of a multi-file module, then import resolution will try to load the multi-file
module in `b`.

To prevent these kinds of conflicts, we strongly recommend that you do not give
the same name to adjacent files and directories.

Importantly, single-file and multi-file modules can co-exist in the same
directory.  For example, the directory structure:

    src/
        a.bry [module a]
        b.bry [module b]
        c/
            d.bry [module c]

is a perfectly acceptable project organization.  

Note that multi-file modules always be constructed in sub-directories of the
import paths.  For example, it would not be acceptable for `a` and `b` to form a
multi-file module named `src` because it would then be impossible to import said
module from within the project.

Users can specify import paths via the command-line interface to the compiler;
however, several import paths are added by default:

1. The input directory passed to the compiler.  If a file was specified as
   compiler input, then this would be the directory containing that file.
2. The global public modules directory `$BERRY_PATH$/mods/pub`
3. The standard library modules directory `$BERRY_PATH$/mods/std`.

Import paths are search in order with the input directory *always* being the
first. Additional import paths specified via the `-I` command line option will
be search after the input directory and before the global public modules
directory.

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

## <a name="mod-decl"/> Module Declarations

Modules can be defined via a **module declaration** which specifies the module's
name. Module declarations must occur as the first statement in a file. The
module name must either be the same as the file name or the same as the
directory name. In the former case, the module is a single-file module.  In the
latter case, the module is a multi-file module.  If no module declaration is
included, then the module is assumed to be a single file module.

Module declarations use the syntax:

```berry
module mod_name;
```

Although module declarations are not required, we recommend that all module's in
a project include one to make explicit where and how they fit in the module
hierarchy.

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
