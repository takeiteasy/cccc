# ASSISI Language Design Reference

> A C superset language built on the JCC compiler.
> Core principle: improvements for C, not a replacement. Stay close to C syntax.
> Not memory-safe-by-default Rust. Not class-based C++. C, but better.
> **Constraint**: features that require a runtime library must be explicitly opted into.
>   Desugaring to plain C AST = always acceptable. New AST nodes needing runtime support = opt-in.

---

## Table of Contents

1. [Vision & Principles](#vision--principles)
2. [Pipeline](#pipeline)
3. [VM Evaluation](#vm-evaluation)
4. [Feature Tiers](#feature-tiers)
5. [Metaprogramming System](#metaprogramming-system)
6. [Syntax Phase](#syntax-phase)
7. [Design Decisions Made](#design-decisions-made)
8. [Open Questions](#open-questions)

---

## Vision & Principles

- Superset of C — all valid C is valid ASSISI
- Stay as close to C syntax as possible
- No classes, no C++ templates
- Feature default: desugar to C with no implicit runtime dependency
- ARC memory management as opt-in safety (`safe { }` blocks), not mandatory
- The escape hatch is *into* safety, not out of it (inverse of Rust's `unsafe`)
- Metaprogramming is the force multiplier — get it right first
- Target: experienced C programmers who want better ergonomics, not a new paradigm
- **ASSISI and JCC are separate**: JCC remains a pure C compiler; ASSISI is an
  optional front-end layered on top, the same way Objective-C is a separate
  frontend in Clang rather than C with flags. A plain `.c` file passed to JCC
  never touches ASSISI code paths. ASSISI features are activated by the file
  extension (`.as`), a CLI flag (`--assisi`), or an explicit opt-in pragma —
  never silently.

---

## Pipeline

```
                        JCC (plain C)
                              ↓
                    [ Tokenizer / Parser ]
                              ↓
                            AST
                              ↓
                   [ Pragma macro expansion ]
                              ↓
                    [ Bytecode compiler ]
                              ↓
               bytecode / LLVM IR / C output


ASSISI source (.as / --assisi flag)
    ↓
[ Syntax phase ]        ← sweet.js-style token-level macro expansion
    ↓
[ ASSISI front-end ]    ← desugars ASSISI-only syntax into C AST nodes
    ↓
    ↘ joins the JCC pipeline at AST ↗
```

The ASSISI layers are **not compiled into JCC by default**. They activate only
when the input is identified as ASSISI (file extension `.as` or `--assisi` flag).
A plain `.c` file never touches ASSISI code paths — no overhead, no behavioural
difference for existing C users.

Two distinct macro layers intentionally:
- **Syntax phase**: token-level, runs before the parser, defines new syntax with hygiene
- **Pragma macros**: AST-level, typed, runs after parsing, introspects and constructs AST nodes

They compose: a syntax macro can expand into a pragma macro call, which then generates AST.

---

## VM Evaluation

### Architecture

- **Register-based** (not stack-based) — RISC-V-flavoured ABI
- 32 general-purpose integer registers + 8 float registers (separate files)
- Hard-wired zero register (`REG_ZERO`)
- ABI: `REG_A0`–`A7` for args/return, `REG_T*` temporaries, `REG_S*` callee-saved
- Instructions stored as `long long` (64-bit) words in `text_seg[]`
- Variable-width encoding: 1–4 words per instruction
- Computed goto dispatch (`goto *op_table[op]`) — ~20-30% faster than switch

### Strengths

- Register-based design is correct for a compiled language target
- Computed goto dispatch is the right performance choice
- Safety instrumentation is unusually deep for an interpreted VM:
  - Stack canaries, CFI shadow stack, bounds checking (CHKB)
  - Null/alignment/type pointer checks (CHKP3, CHKA3, CHKT3)
  - UAF detection, double-free detection
  - Uninitialized variable tracking (CHKI/MARKI)
  - Dangling pointer detection via scope markers (SCOPEIN/SCOPEOUT)
  - Pointer provenance tracking (MARKP/CHKPA)
  - Integer overflow checks
  - All opt-in via flags — zero cost when disabled
- Solid FFI: direct function pointers + libffi for variadics + dlopen/dlsym
- Bytecode persistence with relocation
- LLVM backend in progress (#174–#177) — native AOT, LLVM IR output

### Weaknesses / Known Issues

**Architectural:**
- Dispatch still has 1 C function call per instruction (`vm_eval` → computed goto → `op_NAME_fn`).
  The intermediate `eval1` layer is gone, but a fully-inlined threaded interpreter
  would have 0 calls. Minor but measurable overhead at high instruction rates.
- No GC support — bump-pointer heap, no precise stack maps, no write barriers.
- Float register file is `double` only — no `f32`, no vectors.
- No tail-call opcode.


### VM Improvements Worth Doing

1. Inline op bodies at computed-goto label targets — eliminate the remaining 1 C call per instruction
2. O(capacity) hashmap scan on every `SCOPEIN`/`SCOPEOUT` (`ops.c:1727/1758`, #159)
3. File-scope statics in codegen (`codegen.c:158`, #161) — blocks multi-JCC compilation

---

## Feature Tiers

### Tier 1 — Primary (implement these)

#### Metaprogramming Extensions

The pragma macro system is the language's primary differentiator. See
[Metaprogramming System](#metaprogramming-system) for what's done and what remains.

The generics and syntax phase features are downstream of macros being solid.

#### Generics (extending `_Generic`)

ASSISI generics extend C11's `_Generic` mechanism. The compiler generates
monomorphised specialisations at compile time — no runtime polymorphism overhead,
no virtual dispatch, no runtime library needed.

```c
// Generic function — compiler generates a specialisation per call-site type
generic void swap(T *a, T *b) {
    T tmp = *a;
    *a = *b;
    *b = tmp;
}

int x = 1, y = 2;
swap(&x, &y);        // compiler generates swap_int

double p = 1.0, q = 2.0;
swap(&p, &q);        // compiler generates swap_double
```

Under the hood, the compiler emits `_Generic` dispatch for cases where the type
is known statically, and synthesises concrete specialisations (via pragma macros
or direct codegen) for each type encountered. The result is valid C with no
runtime dependency.

**Generic structs** work the same way — each `List(int)` / `List(float)` becomes
a concrete struct type:

```c
generic struct List(T) {
    T *data;
    int len, cap;
};

List(int) ints = {};
List(float) floats = {};
```

Implementation path: pragma macros already have `jcc_ast_struct_create` /
`jcc_ast_struct_add_member` / `jcc_ast_struct_finalise` for type synthesis.
Generics are sugar that drives those APIs.

#### ARC + `safe { }` Block

ARC is opt-in, scoped to `safe {}` blocks. Outside `safe {}`: plain C semantics.

```c
void example(void) {
    // Plain C here — no ARC, your responsibility

    safe {
        // ARC active: objects allocated here are reference-counted
        // VM safety flags enforced (bounds, null checks)
        // weak references nil-checked by compiler before use
        String s = "hello";   // ARC-managed
    }   // s released here
}
```

**Pointer ownership qualifiers** (inside `safe {}`):
```c
strong  // owns, retains, releases (default — no annotation required)
weak    // non-owning, nullable, zeroed on dealloc — compiler enforces nil-check
unowned // non-owning, non-nullable, non-retaining — "I know this outlives me"
```

**Runtime library tension**: ARC needs retain/release logic *somewhere*. Options:
1. Header-only inline functions (still "no external library" if header-only)
2. Small static lib always linked for `safe {}` builds
3. Emit explicit ref-count struct fields + inline operations into the C output

Option 3 is the most aggressive "no library" interpretation; option 1 is the
pragmatic default. See [Open Questions](#open-questions).

---

### Tier 2 — Secondary (implement after Tier 1)

#### Named + Default Arguments

```c
void connect(String host, int port = 8080, bool tls = false);
connect(host: "localhost", tls: true);  // port gets default
```

Desugars to a plain C call at the call site — no ABI change, no runtime cost.
Challenge: name mangling for separate compilation; C ABI compatibility.

#### try / catch / finally

```c
try {
    result = parse(input);
} catch (ParseError *e) {
    log(e->message);
} finally {
    cleanup();
}
```

Built on `setjmp`/`longjmp` (VM already has `SETJMP`/`LONGJMP` opcodes).
`finally` requires per-function unwind tables. The C output uses `<setjmp.h>` —
standard library, not a custom runtime.

#### Blocks / Closures

Clang/GCC block extension syntax (`^{ }`), already supported by both host
compilers. ASSISI adopts it as the closure syntax.

```c
Block b = ^{ printf("hello\n"); };

// With capture list (prevents retain cycles inside safe {}):
self->on_complete = ^[weak self] {
    if (self) self->finish();
};
```

The `libBlocksRuntime` link requirement is the runtime-library tension here.
Inside `safe {}` this is acceptable; outside it should desugar to a plain
function pointer + context struct.

**Ownership qualifiers interact with blocks** — the `weak`/`strong`/`unowned`
vocabulary applies to captured variables inside `safe {}`.

#### String Interpolation

```c
String s = $"Hello {name}, you are {age} years old";
```

Desugars at compile time to a sequence of format/concatenation calls.
If ARC Strings are opted into, uses the ARC string builder. Otherwise desugars
to `snprintf` into a stack buffer — no runtime library.

`c"hello"` sigil for raw `const char *` literals.

#### Operator Overloading

Two distinct mechanisms, composable:

**1. Overloading existing operators (Swift-inspired)**

Declare a function with the `operator` keyword. The compiler rewrites the
expression to a call when the operand types match — pure compile-time dispatch,
desugars to a plain C function call, no runtime overhead.

```c
// Define + for Vec2
operator+(Vec2 lhs, Vec2 rhs) -> Vec2 {
    return (Vec2){ lhs.x + rhs.x, lhs.y + rhs.y };
}

// Define == for Vec2
operator==(Vec2 lhs, Vec2 rhs) -> bool {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

Vec2 a = {1, 2}, b = {3, 4};
Vec2 c = a + b;     // → operator_add_Vec2_Vec2(a, b)
bool eq = a == b;   // → operator_eq_Vec2_Vec2(a, b)
```

Overloads are looked up by the types of the operands. If no overload exists,
the compiler falls back to the normal C operator (or errors for non-numeric types).
Name-mangling (`operator_add_Vec2_Vec2`) means the C output is valid and linkable.

Permitted operators: `+` `-` `*` `/` `%` `==` `!=` `<` `>` `<=` `>=` `[]` (unary `-` `!` `~`).
Assignment operators (`+=` etc.) auto-synthesised from the binary form unless
explicitly overridden.

**2. New operators (sweet.js syntax phase)**

Entirely new operator tokens are defined via the syntax phase with explicit
precedence and associativity. The syntax macro expands them before the parser,
so the C output only ever sees function calls.

```c
// Declare precedence group
precedencegroup Pipe {
    associativity: left
    lowerThan:     MultiplicationPrecedence
    higherThan:    AssignmentPrecedence
}

// Define the operator token + its fixity
syntax infix operator |> : Pipe {
    ($lhs |> $rhs) => { $rhs($lhs) }
}

// Usage — the syntax phase rewrites before the parser sees it
int result = data |> filter |> transform |> count;
// → count(transform(filter(data)))
```

New operator definitions live in a syntax macro file; they are import-scoped
(Swift/Haskell model) — an operator defined in a module does not leak into
unrelated code that doesn't import it.

**How they compose:** a new syntax operator can expand to an overloaded-operator
call, which the compiler then resolves by type:

```c
syntax infix operator ++ : AdditionPrecedence {
    ($lhs ++ $rhs) => { $lhs + $rhs }   // reuses + overload
}
```

**No runtime cost**: both mechanisms produce plain C function calls in the output.
Overload resolution and syntax expansion are entirely compile-time.

---

### Tier 3 — Deferred / Debatable

These are worth having but are not driving the design. Revisit once Tier 1+2 are solid.

| Feature | Notes |
|---|---|
| Sum types + `match` | The biggest missing C abstraction. Needs exhaustive pattern matching. |
| Slices (arrays with length) | Eliminates `sizeof(arr)/sizeof(arr[0])`. Foundation for safe strings. |
| `let` type inference | Pure ergonomics; low risk. |
| Enum namespacing + exhaustiveness | `Color.Red` instead of `COLOR_RED`. |
| Multiple return / destructuring | `let (ok, val) = try_parse(str);` |
| Range-based `for` | `for (i, item in array)` |
| Traits | Struct methods + UFCS cover the simple cases. Traits needed for generics constraints. |
| Module/namespace system | `module Vec2;` — eliminates `mylib_fn_name` prefix convention. |
| Struct equality | `==` member-wise for value types. |
| Coroutines | Stackful preferred but significant VM work. Depends on defer/try design. |
| Built-in testing | `@test` decorator — implementable via pragma macros now. |
| Built-in build system | Intentional scope question — in-language or separate tool? |
| First-class allocators | Zig-style. Relevant if `safe {}` arena allocator is added. |
| Compile-time reflection | `@fields(T)`, `@sizeof(T)` — subset already available via reflect.c. |

---

## Metaprogramming System

### What the Pragma Macro System Now Does

The pragma macro system has grown significantly. Current capabilities:

1. **Pragma macros are compiled C functions** running in the JCC VM at compile time —
   not a separate interpreter, not text substitution.
2. **Full typed AST construction**: literals, binary/unary ops, if/switch,
   function generation, casts, return, block, expr_stmt, function calls, loops,
   local variable declarations, assignments.
3. **Type introspection**: struct members (name, offset, type, bitfield info),
   enum constants (name, value), type kind/size/align, pointer/array base types,
   function parameter types.
4. **Global symbol injection**: `jcc_ast_function` emits a new `Obj*` into the
   program's global scope.
5. **Declaration-level invocation**: top-level macros can receive and annotate
   struct/function declarations — `@decorator` pattern works.
6. **Recursive macro expansion**: a macro result containing another macro call
   is re-expanded automatically.
7. **Shared compile-time state**: global variables in the macro program persist
   across calls within a compilation.
8. **Quasi-quoting**: `__jcc_quote(vm, "$1 + $2 * $1", a, b)` — write the code
   you want to generate, splice nodes in by position. `$$` for sequential splices.
   `__jcc_quote_n` for array form.
9. **Hygiene**: `jcc_gensym` for fresh names, scoped hygiene prevents capture.
10. **Diagnostics**: `jcc_error_at` / `jcc_warning_at` report errors pointing at
    the macro call site with source location.
11. **Compile-time variables**: `#pragma comptime` variables available to macros.
12. **Inline macros**: macros defined and used in the same translation unit.
13. **Reflection API automatically included**: `reflection.h` is implicitly
    prepended to all macro compilation units.

### The `macro` Keyword

`macro` is syntax sugar over `#pragma macro`. It is recognised by the ASSISI
front-end pass and desugars before the pragma compiler sees it, so every feature
of the underlying pragma system remains available.

**Expression macro** (most common form):

```c
// ASSISI
macro add_mul(a, b) {
    return quote("$1 + $2 * $1", a, b);
}

// Desugars to:
#pragma macro
_Node *add_mul(_Node *a, _Node *b) {
    _VirtualMachine *vm = __jcc_get_vm();
    return __jcc_quote(vm, "$1 + $2 * $1", a, b);
}
```

What the sugar provides:
- Parameters are `_Node *` implicitly — no need to write the type
- `vm` is implicit in the macro body — no `__jcc_get_vm()` call
- `quote(fmt, ...)` is shorthand for `__jcc_quote(vm, fmt, ...)`
- `gensym(prefix)` is shorthand for `jcc_gensym(vm, prefix)`
- `error_at(node, fmt, ...)` is shorthand for `jcc_error_at(vm, node, fmt, ...)`

**Typed parameters** — when a parameter is not a node but a compile-time value:

```c
macro assert_aligned(ty: Type*, boundary: int) {
    if (jcc_type_align(vm, ty) % boundary != 0)
        error_at(NULL, "type is not %d-byte aligned", boundary);
    return quote("(void)0");
}
```

Typed parameters use `name: Type` syntax and are passed as their declared C type,
not wrapped in `_Node *`. The `Type *` annotation maps to `_Type *` in the
desugared form; `int`, `long`, `const char *` etc. map directly.

**Inline macro** — runs at its declaration point, no explicit call needed:

```c
inline macro register_commands(void) {
    // generates registration boilerplate at the point this appears
    ...
}
```

Desugars to `#pragma macro` with the `inline` qualifier, same semantics as
the existing inline pragma macro.

**Decorator macro** — applied to a declaration with `@`:

```c
@macro serialize(ty: Type*) {
    // receives the struct's Type*, emits to_json / from_json helpers
    _Node *fns = /* ... build function nodes ... */;
    return fns;
}

@serialize
struct Config { int x; int y; };
```

The `@name` before a struct or function declaration is the existing top-level
macro invocation path — `@macro` just declares that a function is a decorator
macro rather than a regular one, enabling the `@name` call syntax.

**Comptime variables** — the `comptime` keyword is the equivalent sugar for
`#pragma comptime`:

```c
comptime int version = 3;       // → #pragma comptime int version = 3;
comptime char *build = "debug"; // available to all macros in the TU
```

**Summary of desugaring rules:**

| ASSISI surface | Pragma form |
|---|---|
| `macro f(a, b) { body }` | `#pragma macro` + `_Node *f(_Node *a, _Node *b)` + implicit `vm` |
| `macro f(x: Type*) { }` | parameter becomes `_Type *x` |
| `inline macro f() { }` | `#pragma macro` + `inline` qualifier |
| `@macro dec(ty: Type*)` | top-level macro, callable via `@dec` |
| `quote("...", a, b)` | `__jcc_quote(vm, "...", a, b)` |
| `gensym("prefix")` | `jcc_gensym(vm, "prefix")` |
| `error_at(n, "...")` | `jcc_error_at(vm, n, "...")` |
| `comptime T x = v` | `#pragma comptime T x = v` |

The underlying `#pragma macro` syntax remains valid — `macro` is strictly
additive sugar, not a replacement.

### What Remains

**Type creation from macros** (needed for generics)

Can introspect structs but synthesising new types is not yet wired to generics.
The APIs (`jcc_ast_struct_create` etc.) exist; the generics front-end driving
them does not.

**Typed unquote-splicing** (#172)

`__jcc_quote` currently splices nodes positionally. Typed splicing — where the
splice knows the expected type and can coerce — is open.

**Synthetic tokens + source locations** (#173)

Generated nodes currently inherit the call-site source location. Synthetic
tokens would let macros produce nodes with their own location information,
improving error messages for generated code.

**8-argument limit**

`execute_pragma_macro` silently drops args beyond the 8th. Needs proper
argument-passing (array or stack) for macros with many parameters.

---

## Syntax Phase

### Motivation

The pragma macro system is AST-level: it operates on *typed, parsed* nodes.
That's powerful but means you can't change what the parser sees. You can't
define `unless (cond)` as syntax, or new infix operators, or new statement forms.

The syntax phase runs *before* the parser. It is inspired by Mozilla's
[sweet.js](https://sweetjs.org/) hygienic macro system:

- Macros operate on **tokens**, not AST nodes
- **Hygiene by default**: introduced identifiers are scoped to the macro,
  preventing accidental capture of user variables
- Macros can define **new syntax**: new keywords, statement forms, operators
- Macros can **pattern-match on token sequences** with binding

### Two Layers, Composable

```
syntax macro  →  token sequence  →  parser  →  AST  →  pragma macro  →  final AST
```

A syntax macro can expand to code that calls a pragma macro. This lets syntax
macros define ergonomic surface syntax while pragma macros do the heavy
typed-AST lifting.

Example — `for_each` as a syntax macro:

```c
syntax for_each(item in expr) body {
    // expands to: for (typeof(expr[0]) item = ...) body
}
```

This is not achievable with pragma macros alone because the parser has to
understand the syntax to parse it.

### Design Principles (from sweet.js)

**Hygiene**: variables introduced by a macro expansion are renamed to avoid
capturing variables in the expansion context. The inverse also holds: free
variables in the macro body refer to the macro's definition scope, not the
call site.

```c
syntax swap(a, b) {
    let tmp = a;   // 'tmp' is hygienic — won't shadow a 'tmp' at call site
    a = b;
    b = tmp;
}
```

**Referential transparency**: a macro's meaning doesn't change based on where
it is called. Identifiers in the macro template resolve in the macro's scope.

**Pattern matching on token sequences**: macros declare what token patterns
they match, with named bindings for sub-patterns.

### Syntax Macro vs Pragma Macro — When to Use Which

| Need | Use |
|---|---|
| New statement/keyword syntax | Syntax macro |
| New operator or infix form | Syntax macro |
| Hygiene is the main requirement | Syntax macro |
| AST introspection (struct fields, types) | Pragma macro |
| Code generation (emit functions, structs) | Pragma macro |
| Compile-time computation | Pragma macro |
| Both new syntax + code generation | Syntax macro → pragma macro |

### Implementation Path

1. Token-level macro table: map identifier → syntax transformer function
2. Pattern language: token sequence patterns with captures (similar to Scheme `syntax-rules`)
3. Hygiene renaming pass: introduced identifiers get unique suffixes at expansion time
4. Integrate into pipeline as a pre-parse pass operating on the token stream

The existing tokenizer (`tokenize.c`) produces the token stream that the syntax
phase would consume and rewrite before `parse.c` sees it.

---

## Design Decisions Made

| Decision | Choice | Rationale |
|---|---|---|
| Generics implementation | Monomorphised, `_Generic`-extended | No runtime cost, fits C semantics |
| String/C interop | `c"hello"` sigil for raw literals, managed default | Least commitment, safest default |
| ARC pointer vocabulary | `strong`/`weak`/`unowned` (no `__`) | Swift simplified ObjC correctly |
| `weak` must be nullable | Yes, compiler enforces nil-check in `safe{}` | Forces correctness at use site |
| Block syntax | `^{ }` Clang/GCC extension style | Existing compiler extension, proven |
| Capture lists | `^[weak self]{ }` | Cleaner than ObjC's manual alias |
| Macro quasi-quoting | `__jcc_quote` positional splice | Implemented, ergonomic for common cases |
| Syntax phase placement | Pre-parse token rewriting | Can define new syntax; composable with AST macros |
| No classes | Confirmed | Struct methods + traits cover the use cases |
| No C++ templates | Confirmed | Monomorphised generics instead |
| No package manager | Confirmed | Intentional scope limit |
| Runtime library | Header-only or static for ARC; desugar-to-C for everything else | No invisible runtime cost |

---

## Open Questions

### ARC: Header-only, static lib, or inline expansion?

The "no runtime library" constraint has three interpretations:
1. **Header-only inline functions** — ships as a `.h`, no link step (pragmatic default)
2. **Small static lib** — always linked when `safe {}` is used, but no dynamic dep
3. **Inline into output** — compiler emits all retain/release as local functions in the C output

Option 1 is the Objective-C model (the ARC runtime was a library, but it was
always present). Option 3 gives the cleanest "paste this C file, no deps"
guarantee but generates more code. **Decision needed before implementing ARC.**

### Blocks (`^{ }`): inside vs outside `safe {}`

Clang blocks need `libBlocksRuntime` unless the runtime is embedded.
- Inside `safe {}`: ARC is already opted into, so the blocks runtime is acceptable
- Outside `safe {}`: desugar to a function pointer + context struct (no runtime)

Is this two-tier approach acceptable, or should blocks be uniformly desugar-to-C?

### Generics constraints: traits now or later?

Unconstrained generics (`generic void swap(T)`) are safe to implement first.
Trait-constrained generics (`<T: Printable + Comparable>`) need the traits
system to be designed first. Options:
1. Ship unconstrained generics now, add constraints when traits exist
2. Design a minimal constraint syntax that doesn't require full traits (`<T: has .print>`)

### Syntax phase: syntax-rules style or procedural?

Two models for how syntax macros are written:
- **Declarative** (Scheme `syntax-rules`, Rust `macro_rules!`): pattern → template
  rewrite rules, purely structural, guaranteed termination
- **Procedural** (sweet.js, Rust proc macros): transformer functions that receive
  a token stream and return a token stream, arbitrary computation allowed

Declarative is safer and simpler to implement; procedural is more powerful and
consistent with the existing pragma macro approach. Given the pragma macro system
is already procedural, a procedural syntax phase is the natural choice.

### `match` keyword vs improved `switch`?

(Deferred until sum types are scheduled, but the question remains.)
New `match` keyword is cleaner; extended `switch` is more C-compatible.
No-implicit-fallthrough default should apply regardless.

### `safe { }` and calling plain C from within?

Raw pointers returned from C functions inside `safe {}`: unmanaged.
Only types declared in ASSISI with ARC annotations get retain/release treatment.
Need a clear rule for the managed/unmanaged transition at the boundary.

---

*Updated 2026-06-02. Previous version 2026-05-30.*
*Update as decisions are made and features are implemented.*
