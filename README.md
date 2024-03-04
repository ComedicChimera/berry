# The Berry Programming Language

Berry is small, compiled programming language.  I have been developing it
primarily for my own enjoyment and challenge over the past few weeks.  As of
now, it is already Turing complete, but I have not yet implemented support for
any platforms other than Windows (the one I work on).

I am first and foremost a full-time college student and research assistant, so I
don't always get time (or energy) to work on it.

## Roadmap 

- [x] Target 1: Hello World

- [x] Target 2: Conditionals, Loops, and Arithmetic

- [x] Target 3: Arrays and Strings

- [x] Target 4: Modules and Multi-File Projects

- [ ] Target 5: Structs

- [ ] Target 6: Constants and Global Initializers
    * Init Ordering and Cycle Detection
        - Detect cycles in functions and global initializer
    * Automatically detect when a global initializer is constant
        - Only put initializers in `__$init` which aren't constant
        - Remove empty `__$init` functions
        - Runtime Globals won't be overwritten by `runtime.__$init`
    * Constants are compile-time constant (comptime, like Go)
        - Use LLVM constant instructions to facilitate compile-time evaluation
        - No need to do compile-time evaluation yourself
        - No local constants: not needed.
    * No pointers to constants: aren't guaranteed to have a well-defined location
        - May be automatically inlined by the compiler (like defines)
    * No constant parameters, fields, or anything besides variables
    * Enforce string immutability (introduce `string` type)

- [ ] Target 7: Enums and Aliases
    * Simple C-style enums (no value-storing variants)
    * Basic Match Statements
        - Minimal pattern matching (variable captures only)
        - Multiple Values in a Case
    * Test Match Expressions
    * Builtin Type Aliases
        - `byte` and `rune`

- [ ] Target 8: Non-Numeric Equality
    * Pointer Comparison
    * String Comparison
    * Struct Comparison
    * Enum Comparison

- [ ] Target 9: Meta Directives and Intrinsic Macros
    * Conditional Compilation
        - `#require` and `#if`
        - Builtin Defines: `OS`, `ARCH`, `COMPILER`, and `DEBUG`
        - No user defined symbols (right now)
    * `@sizeof` and `@alignof`
    * Replace unsafe casting with `@unsafe_cast` 
        - Unsafe Casts = ptr to ptr, string to byte array, byte array to string
    * Pointer arithmetic with `@ptr_offset`
    * Extract defines in code via `@get_defined`
    * Platform Sized Ints: `int` and `uint`

- [ ] Target 10: Dynamic Memory, Threads, and Panicking

- [ ] Target 11: Garbage Collections and Automatic Allocation

- [ ] Target 12: Debug Info
    * Fix DIType generation
    * Add code for debug assignment
    * Get debugging working on Windows (enable stepping through the program)

