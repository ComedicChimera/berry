# The Berry Programming Language

## CLI Info

```
Usage: berry [options] <filename>

Flags:
    -h, --help      Print usage message and exit
    -d, --debug     Generate debug information
    -v, --verbose   Print out compilation steps, list modules compiled
    -V, --version   Print the compiler version and exit
    -q, --quiet     Compile silently, no command line output

Arguments:
    -o, --outpath   Specify the output path (default = out[.exe])
    -E, --emit      Specify the output format
                    :: exe (default), static, shared, obj, asm, llvm, astdump
    -g, --gendebug  Specify the debug format, automatically enables debug info
                    :: native (default), gdb, msvc
    -L, --libpath   Specify additional linker include directories
    -l, --lib       Specify additional static libraries, shared libraries, or objects
    -W, --warn      Enable specific warnings
    -w, --nowarn    Disable specific warnings
    -O, --opt       Set optimization level (default = 1)
```

Example: Output linked LLVM to `out.ll` with debug info:

    berry -d -Ellvm -o out.ll mod.bry

    // Equivalently:
    berry --debug --emit=llvm --outpath out.ll mod.bry

For single tac options, you can specify values with a space, an `=`, or directly
after the option name. For double tac options, you can specify values with a
space or with an `=`.  All option parsing is Unix style.

## Notes

TODO: move to a more appropriate location.

### Struct Enums

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

### Heap Allocation

Remove `&` for r-value references (no implicit heap allocation).

Instead, use `new` which explicitly has the possibility to allocate on the heap.

```
new Struct{x=10}   // New *Struct
new int[10]        // New integer array of 10 elements
new List[int]{}    // New generic type
```

### Implement Intrinsics as "Macros"

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

### Use Language as Build Tool

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

### Alternative Method Syntax

Try to figure out which syntax works the best.

Option A: "Group/Go Style":

```
func List[T].Foo() {
    // Regular method

    // Members are accessible w/o need for explicit self-reference.
}

static func List[T].StaticFoo() {
    // Static method
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

    // Static method
    static func Foo();
}

interf for List[T = int] {
    // Specialization
    func Foo();
}
```

### Conditional Compilation

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

### Chaining Interface

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

### Testing Example

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