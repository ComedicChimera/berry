# Feature Notes

This file lists some of the *planned* features for Berry.  These are mostly just
design ideas I have had as I have been working on it.

## Constructors

Constructors are special functions that can be defined for any type using an
interface binding. They use the syntax:

```
constructor() Type {

}
```

where `Type` can either be the type being constructed or a pointer to it.  For
example, the constructor for a `List[T]` returns `*List[T]`.  Note that unlike
most OOP languages, constructors in Berry are just special functions: they are
responsible for creating the instance of the type.  Using the `List[T]` example,
the constructor for a list would look like:

```
constructor() *List[T] {
    return new List{
        .arr = new T[LIST_INIT_SIZE],
        ._len = 0
    };
}
```

Constructors are invoked by directly calling the type you want to construct:

```
let list = List[int]();  // Calls the List constructor
```

Constructors can be overloaded like regular functions: a type can have multiple
constructors with different signatures.

Note that because we are now supporting constructors, static methods are no
longer "necessary" or idiomatic.  They will be removed as a planned feature.

## Conditional Binding

Create chaining contexts using specialized `if` statements and `while` loops.
These syntax and semantics replace the old notion of a "context manager".

```
if x <- fn() {
    // ...
} else Err(e) {
    // ...
}

while x <- fn() {

} else Err(e) {

}
```

Both of these features create new chaining contexts.  All errors which occur
inside these contexts will bubble to the else statement rather than to the
return value of the enclosing function.

## Let-Bindings and the Question Operator

The question-mark operator works much like it does in Rust.

```
next()?; // Fails to enclosing chain context
```

Its principle use is in expression statements or deeply nested expressions. To
replace the old planned functionality for `?`, we will instead use local `let`
bindings:

```berry
let x <- safe_sqrt(y) in x + 2
```

These can also be non-monadic for doing variable binding in expressions.

## Multi-Case Matching

To make over multiple possibilities in a case clause, match expression or
test-match expression, we use pipes `|` in the patterns rather than `,` (to
allow for implicit, ergonomic tuple unpacking).

```
// Match Case
match peek()? {
case '\r' | '\t' | '\v' | '\f' | ' ' | '\n':
    skip()?;
}

// Test-Match (really good for checking multiple values quickly)
if x match A | B | C {
    // -- snip --
}
```

## Struct Enums

Example from Compiler Code (very ergonomic):

```
enum AstDef {	
	span: *TextSpan;
	
	Func{
		symbol: *Symbol;
		params: []*Symbol;
		return_type: *Type;
		body: AstExpr; 
	};
	GlobalVar{
		symbol: *Symbol;
		init: AstExpr;
	};
}

func checkFunc(fn: *AstDef.Func) {
	defineGlobal(fn.Span, fn.symbol);

	// -- snip --
}

func checkDef(AstDef def) {
	match def in v {
	Func:
		checkFunc(v);
	GlobalVar:
		checkGlobalVar(v);
	}
}
```

## Heap Allocation

Remove `&` for r-value references (no implicit heap allocation).

Instead, use `new` which explicitly has the possibility to allocate on the heap.

```
new Struct{x=10}   // New *Struct
new int[10]        // New integer array of 10 elements
new List[int]{}    // New generic type
```

## Implement Intrinsics as "Macros"

These are typically compiler intrinsics and cannot be treated as first class
functions.  Later, we can give uses support to create their own.

Syntax: `@func = macro`.

Example:

```
func hash(f: f64) u64 = @bitcast(f, u64);

struct LLNode {
    value: int;
    next: Option[LLNode];
}

func main() {
    let root = @bitcast(runtime.MemAlloc(@sizeof(LLNode)), *LLNode);
    // Equiv to: let root = &LLNode{}
}
```

## Use Language as Build Tool

Provide a standard library module called `make`.  Add the command `berry make`
into the compiler which builds and runs the "make" script appropiately.  This
acts as a replacement for the usual methods of build configuration.

```
import make;
import proc;
import io;

func main() {
    let build = make.NewBuild();

    build.AddMod("main.bry");
    build.SetFlag(make.WARN_ALL);

    let p_stdout = proc.NewOutPipe();
    defer p_stdout.Close();

    let status = proc.Run("llvm-config", "--includedir", stdout_pipe=p_stdout);
    if status < 0 {
        exit(1);
    }

    let include_path = io.ReadAllText(p_stdout).Strip();
    build.AddIncludeDir(include_path);

    build.Run();
}
```

## Alternative Method Syntax

Try to figure out which syntax works the best.

Option A: "Group/Go Style":

```
func List[T].Foo() {
    // Regular method

    // Members are accessible w/o need for explicit self-reference.
}

func List[T = int].Foo() {
    // Generic Specialization
}
```

Option B: "Interface/Java Style":

```
interf for List[T] {
    // Regular Method
    func Foo();
}

interf for List[T = int] {
    // Specialization
    func Foo();
}
```

## Conditional Compilation

Example:

```
#if OS == "windows" {
    // Windows only
} else { 
    // Unix, etc.
}

```

It can also be applied at the file-level: 

```
#require OS == "windows";
```

For fancy stuff with braces, you can use `#begin` and `#end` instead of `{}`:

```
#if MY_VAR #begin
{
#end

// -- snip --

#if MY_VAR #begin
}
#end
```

## Chaining Interface

Chaining is our implementation of "monads".

```
import math

interf Chain[T] {
    func Test() Option[T];

    func Fail<R>() Chain[R];
}

enum Result[T] {
    Ok(T);
    Err(Error);
}

interf for Result[T] is Chain[T] {
    func Test() Option[T] =
        match self {
            .Ok(x) => .Some(x),
            .Err(_) => None
        };

    func Fail[R]() Chain[R] = 
        match self {
            .Ok(_) => unreachable(),
            .Err(e) => .Err(e)
        };
}

func SafeSqrt(n: f64) Option[f64] {
    if n < 0 {
        return .None
    }

    return .Some(math.Sqrt(n))
}

func QuadraticFormula(a, b, c: f64) Option[(f64, f64)] {
    let delta = b ** 2 - 4 * a * c;
    let sd <- SafeSqrt(delta);

    return (
        (-b + sd) / (2 * a),
        (-b - sd) / (2 * a)
    );
}
```

## Testing Example

```
func IsEven(n: int) bool = n % 2 == 0;

#test_suite "is_even" {
    #test_object t

    #test "even" {
        t.AssertEqual(IsEven(4), true);
    }

    #test "odd" {
        t.AssertEqual(IsEven(3), false);

    }
}
```