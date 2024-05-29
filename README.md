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

- [x] Target 5: Structs

- [x] Target 6: Constants and Global Initializers
    * Init Ordering and Cycle Detection
        - Detect cycles in functions and global initializer
    * Automatically detect when a global initializer is constant
        - Only put initializers in `__$init` which aren't constant
        - Remove empty `__$init` functions
        - Runtime Globals won't be overwritten by `runtime.__$init`
    * Constants are compile-time constant (comptime, like Go)
        - Support defining local constants
    * No pointers to constants: aren't guaranteed to have a well-defined location
        - May be automatically inlined by the compiler (like defines)
    * No constant parameters, fields, or anything besides variables
    * Enforce string immutability (introduce `string` type)

- [x] Target 7: Enums and Aliases
    * Simple C-style enums (no value-storing variants)
    * Basic Match Statements
        - Minimal pattern matching (variable captures only)
        - Multiple Values in a Case
    * Test Match Expressions
    * Type Aliases
        - Builtin Aliases: `byte` and `rune`

- [x] Target 8: Non-Numeric Equality and Pointer Arithmetic
    * Pointer Comparison
    * String Comparison
    * Enum Comparison
    * Boolean Comparison
    * C-Style Pointer Arithmetic
    * Matching over Strings
    * Allow `ptr == null` to infer type of `null`.

- [x] Target 9: Improved Attributes
    * Rename metadata tags to attributes.
    * Add mutual exclusivity checking to attributes.
    * Update `abientry` and `extern` to take an optional argument.
        - If the argument is present, then it becomes the name of the exported
          ABI symbol.  The argument can be any string value.
        - This allows for ABI symbols which are not valid Berry identifiers.
        - Ex: `@abientry("__berry_strhash")`
    * Make sure constants can't be tagged with variable attributes.
    * Only allow `@callconv` on external functions.
    * Add support for `@inline`

- [x] Target 10: Meta Directives, Unsafe, and Intrinsic Macros
    * Conditional Compilation
        - `#require` and `#if`
        - Builtin Defines: `OS`, `ARCH`, `ARCH_SIZE`, `COMPILER`, and `DEBUG`
        - The meta language is purely string-based.
            * Only has `==`, `!=`, `!`, `&&`, and `||`
            * Empty string is false, anything else is true
            * If no symbol exists, it corresponds to the empty string
            * Can use string literals
        - No user defined symbols (right now)
    * Unsafe Blocks (`unsafe`)
        - A specialized block statement not a directive.
            * Implementing it as a directive seems like more effort than its worth.
        - Only allow pointer arithmetic and unsafe casting inside unsafe blocks
          and functions
        - Replaces the need for a bunch of extra macros.
        - Unsafe Casts:
            * Int to Ptr, Ptr to Int, Ptr to Ptr
            * Int to Enum
            * String to Byte Array
    * `@sizeof` and `@alignof`
    * Extract defines in code via `@defined`
    * Platform Sized Ints: `int` and `uint`
        - Update compiler code to user platform-sized integers where appropriate

- [x] Target 11: Arrays and Slices
    * Rename variable length arrays to slices
    * Add a fixed size array type: `[N]Type`
    * All array constants default to array.
    * Arrays implicitly convert to slices

- [x] Target 12: Error Handling
    * Factor platform bindings into `sys.windows`
    * Signal Handling
        - Stack Overflow
        - Access Violation
        - Int and Float Errors 
    * Proper Handling of Divide by Zero and Invalid Shifts
        - Capture poison values as they occur
    * Use `llvm.expect` to speed up checks for optimizer

- [x] Target 13: Methods
    * Method Binding
        - For named types: `Name.method()`
        - For imported named types: `pkg.Name.method()`
        - Can only be defined for named types (for now)
    * Method Calling
    * Method importing and scoping
    * Access to self pointer via `self`
    * Factory functions
        - Named Types: `factory Name(x)`
        - Imported Named Types: `factory pkg.Name(x)`
        - Can only be defined for named types
        - Called via writing `Name()`
    * Anonymous Importing
        - `import pkg as _` imports method table with polluting namespace

- [ ] Target 14: Dynamic Memory and TLS
    * Fix pointer arithmetic
        - Addition: Only allow pointer + integer (forward offset)
        - Subtraction: pointer - integer (backward offset), pointer - pointer (pointer diff)
        - Ordinal Comparison: pointer (cmp) pointer
        - No other extra operations are legal
    * Unsafe Expressions: `unsafe(expr)`
    * Atomics
        - `@atomic_cas_weak`
        - `@atomic_load`
        - `@atomic_store`
    * Thread Local Storage + TL Runtime State
        - Allocate runtime state on heap
    * Multithreaded Allocator
        - Good enough to last a while
        - Should support multi-threading
        - Based off mi-malloc

- [ ] Target 15: Garbage Collection
    * Mark-and-Sweep GC
        - Write it single threaded for now (later adapt it to be STW).
        - No support for polymorphic types (require type pointer maps).
        - Lazy sweeping: allocator will sweep thread-local garbage when it needs
          more space for allocation (as a part of the generic malloc path).
        - Generation of stack and register maps
        - Efficient stack crawling for marking
    * Automatic Heap Allocation
        - All allocations with `new` default to heap allocation.
        - Slice literals also default to heap allocation.
        - Global memory is allocated on the heap unless it is comptime.  This allows
        us to mutate the pointer value without creating a memory leak.
    * Escape Analysis
        - Optimization pass determines whether an allocation with `new` or that
        of a slice literal can be stored on the stack instead of the heap.
        - References via `&` can "escape" to the heap if the compiler determines
        that their lifetime may be greater than their enclosing function.  This
        is done via a simple alloc-and-copy.
    * Use of `unsafe` to disable garbage collector behavior in code which messes
    with memory or uses pointers in unsafe ways (ex: inside the allocator).
        - Remove unsafe expressions.
        - Add the ability to tag declarations as unsafe which makes their whole
        predicate unsafe (ex: disable GC throughout whole function).  This
        replaces the need for unsafe expressions for declaring unsafe constants.
        - Make it possible to declare files as being unsafe (ex: `malloc.bry`)
        to avoid having to declare every single thing inside it as unsafe. 
    * Allow the GC to mark certain pages is "unmanaged" making them ineligible
    for collection.  This also ensures that the page has no mark bitmap.
        - Perhaps the absence of a mark bitmap could indicate that a page is
        unmanaged or we could use a page flag to indicate it.

- [ ] Target 16: Multiple Threads
    * Function Type labels
        - No closures yet, just regular old functions
    * Linking and compiling asm files
        - For now, just set the compiler to always compile the appropriate
          `asm/rt_[os]_[arch].asm` file.
    * Runtime Thread Support
        - Creation of RtThreads
        - Management of Threads and TLS state
        - Thread shutdown on panic
    * Threads and GC Interactions
        - Add GC check points to enable GC to stop the world via a barrier.
        - Ensure that threads can only be suspended at GC check points, so
        that a collection can occur without waking them.
        - Explore the possibility of doing parallel marking using all threads:
        all the threads are already going to rendezvous at a GC check point and
        if we are clever, it may be possible to allow all the threads to mark
        from their own roots (stacks and registers) in parallel.
    * Expose thread functionality through `threads` package
        - Wrap `RtThread` into `Thread`
        - Thread methods: `get_id`, `suspend`, `wait`, etc.
        - Utility Method: `sleep`, `sleepms`, `get_current_thread`
    * Test compatibility with dynamic memory

- [ ] Target 17: Better Functions
    * Variadic Arguments
    * Named and Optional Arguments
    * Function Overloading

- [ ] Target 18: Interfaces
    * Interface Declarations 
    * Interface Inheritance
    * Virtual methods
    * The `any` type
    * `is` Assertions

- [ ] Target 19: Formatted IO
    * Make `io.std` more usable `std.println`, etc...

- [ ] Target 20: Pattern Matching
    * Tuple Pattern Matching
    * Struct Pattern Matching
    * Tuple and Struct Bindings (variables, assignment, etc.)

- [ ] Target 21: Tagged Unions
    * Enum-like Variants
    * Tuple-like Variants
    * Struct-like Variants
    * Shared Fields
    * Variant Types (use variants as pseudo-struct types)
        - Ex: `AstExpr.Call`, `Type.Named`, etc.
    * `is` Assertions for Variant Types

- [ ] Target 22: Generics
    * Generic Type Inference
    * Generic Types and Functions
    * Generic Method Binding
    * Interface Constraints  

- [ ] Target 23: Iterators
    * The `Iter` and `Seq` interfaces
    * Builtin methods: `.len()` and `.iter()`.
    * For each loops

- [ ] Target 24: Monadic Error Handling
    * The `Chain` interface
    * The `?` and `<-` Operators
    * Conditional Binding
        - Ex: `if x <- fn()` 

- [ ] Target 25: Operator Overloading

- [ ] Target 26: Builtin Collections
    * `List[T]`
    * `Map[K, V]`
    * `Set[T]`
    * `@map` macro for fast map creation
    * Builtin collection methods: `.at()`
    * Builtin hashing: `.hash()`
    * The `Col` interface 

- [ ] Target 27: Closures and Defer
    * Lambda functions: `|x, y| => expr`
    * Lambda type inference
    * Block lambda functions: `|x, y| { ... }`
    * Closures and capture detection
    * The `defer` statement
    * Method references (using `value.method` as a value)

- [ ] Target 28: Sequences
    * Builtin sequence methods like `.map`, `.filter`, etc.
    * The `sequences` package

- [ ] Target 29: Better Generics
    * Generic control flow
    * Variadic generics

- [ ] Target 30: Better Type Constraints
    * Or (`|`) and And (`&`) Constraints
    * Defined Constaints (Traits)
    * Operator Constraints
    * Builtin Numeric Constraints (`Int`, `Float`, `Num`)

- [ ] Target 31: Debug Info
    * Fix DIType generation
    * Add code for debug assignment
    * Get debugging working on Windows (enable stepping through the program)
    * Proper handling of breakpoints and state dumping
    * Stack Traceback Support








