# Feature Notes

This file lists some of the *planned* features for Berry.  These are mostly just
design ideas I have had as I have been working on it.

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