# Refocus plan: a leaner parser

**Status: open.** Direction agreed; no work started. Tracked by #2178 (label REWRITE).

cccc keeps its compiler and cleans it up incrementally on trunk. Extensions that work on tokens alone move out of the parser into **syntax macros and readers**; `<=>` and other user operators move to **operator definitions**. Everything that needs types stays in the parser, cleaned up where it can be but not redesigned.

## Direction

| | Today | Refocus |
|---|---|---|
| Main job | Run any C program in the VM, emulating gcc/clang | Parse, run comptime, lower to C for the host compiler |
| The VM | Full program runtime with broad POSIX emulation | Mainly the comptime engine; running whole programs stays |
| Token-level extensions | Hand-written in the parser | Syntax macros and readers |
| Type-level extensions | Hand-written in the parser | Stay in the parser |
| Vendor attributes | Reimplemented or silently dropped | Understood when they change types; otherwise passed through |
| Strategy | — | Incremental on trunk: no branch, no freeze |

## Principles

- **One change at a time.** Each move keeps its suites passing and deletes the parser code it replaces. Self-hosting (`ouroboros`) and the full suite stay green after every step.
- **Tokens out, types in.** A feature leaves the parser only if it can be written without knowing types.
- **"The VM is for comptime" steers new work; it is not a deletion list** ([VM scope](#vm-scope)).

## Feature inventory

| Feature | Destination | Why |
|---|---|---|
| `@name(args)` | Syntax macro | Tokens only |
| Methods and operators written in a struct body | Syntax macro, rewritten to the free spelling (`[[cccc::method(T)]]`, `[[cccc::operator(+)]]`) after the declaration | Tokens only; the type name is in the text |
| `a <=> b`, user infix operators | Operator definitions | Needs a precedence, not types |
| Foreign syntax (SQL, Lisp, format strings) | Readers | Raw text |
| `s.f()` method calls | Parser | Needs the receiver's type |
| Operators on struct operands | Parser | Needs operand types |
| `T.f()`, `T(args)`, `f(.name = v)`, `@ctor`, `@default` | Parser | Depends on whether `T` is a type, and on the parameter list |
| Templates, `@specialize`, packs, deduction | Parser | Needs types |
| Checked pointers | Parser | Needs types |
| Overloading (`overloadable`, `enable_if`) | Parser | Changes which function is called; the macOS SDK's `tgmath.h` and `simd/*.h` use it |
| Derive, `^^name`, quasi-quotes, `@test` | Comptime (already) | — |
| Kernels, OpenCL | Frozen ([VM scope](#vm-scope)) | No host C compiler accepts them |
| `_Decimal*`, `_BitInt(N>128)`, native shims, build system, REPL, watch, debugger | Stay | — |

## Syntax macros, readers and operator definitions

| Layer | Runs | Tickets |
|---|---|---|
| Syntax macros | During the parse, tokens in, tokens out | #1645, #1701, #1703, #1704 |
| Readers | On raw text | #1702, #1705 |
| Operator definitions | During the expression parse, with a precedence | — |

```c
@comptime(infix("<=>", RELATIONAL))
Node *cmp3(Node *lhs, Node *rhs) { ... }
```

- A syntax macro defined in the same file runs mid-parse: comptime is interleaved with parsing, not a prior phase.
- The file is tokenized before its own operator definitions run, so the expression parser matches adjacent punctuators (`<=` then `>` with no space) rather than retokenizing.
- Struct-body methods follow the rules in [methods.md](methods.md#defining-in-the-struct): the function is named after the typedef (which comes after the body) or the tag, so the macro reads to the end of the declaration before writing the free functions.

## VM scope

The POSIX library (sockets, mqueue, termios, aio, …), OpenCL and kernel emulation are **frozen**: kept and tested, but no new work. Later they move to an **optional layer**, a separately built library of host functions or a `cccc-run` binary, so the core shrinks without losing them. The VM already registers host functions by name with capability classes, which is the seam.

## Execute, understand, preserve

Every construct gets one of three roles:

| Role | Promise | Applies to |
|---|---|---|
| **Execute** | The VM runs it | Comptime code, and user code that comptime calls |
| **Understand** | Types, layout and constant values are exact | Anything reflection, type checking or lowering depends on |
| **Preserve** | Kept as written and passed through into the output | Everything else; the host compiler deals with it |

| Construct | Role |
|---|---|
| `packed`, `mode(DI)`, `vector_size`, `overloadable` | Understand: they change layout or which function is called |
| `format(printf, 1, 2)`, `nonnull`, `hot`, `cold`, `target` | Preserve |
| `__builtin_ia32_*` | Understand (a type signature) only; "not available at compile time" if comptime reaches it |

A construct that changes a type, a layout or a constant value is never passed through unmodelled: if cccc does not model it, that is an error.

## Layout fixes

`mode`, `transparent_union`, `may_alias`, `weak` and `alias` are recognized and ignored today. `typedef int di __attribute__((mode(DI)))` gives `sizeof(di) == 4` under cccc and 8 under clang (#2177). Each is either modelled or rejected.

## Pass-through

Today unknown attributes are dropped from lowered output: `-m` loses `hot`, `ms_abi`, `frobnicate(1, x)` and `[[vendor::thing]]`. Unknown content is kept in **opaque slots**, places where C23 and GNU syntax already wrap a balanced run of tokens:

| Slot | Example | Kept |
|---|---|---|
| GNU attribute | `__attribute__((ms_abi, foo(1, x)))` | Name and argument tokens |
| C23 attribute | `[[vendor::thing(a, b)]]` | Namespace, name, argument tokens |
| `__declspec` | `__declspec(dllexport)` | Name and argument tokens |
| Asm label | `int f(void) __asm__("_f$v2");` | The string |
| Pragma | `#pragma ...`, `_Pragma(...)` | The token line |

- Each item is a record (namespace, name, placement, location, raw argument tokens) in a new lazily allocated AST group, attached to its owning node.
- The serializer writes it back in the same place.
- Lowering is silent; running in the VM warns once per name.
- Lowered output carries `_Static_assert` checks of `sizeof`/`offsetof` for every type reflection relied on, so an unknown layout attribute fails the host build instead of desyncing.

## Host persona probe

`__has_attribute`, `__has_builtin` and the predefined macros under `--output-compiler` come from hand-written `cc` masks in `src/attr_registry.c` and `src/builtin_registry.c`. Replace them with a probe of the host compiler (`cc -dM -E` plus one batched file of `__has_attribute`/`__has_builtin` tests), cached per compiler path and version. Cross-compiling probes the target compiler; a fallback table answers when no compiler is installed. Running in the VM keeps cccc's own answers.

## Open questions

| Question | Options |
|---|---|
| Readers and the preprocessor | Readers run before preprocessing · after it; and how a reader's text ends |
| Diagnostics on lowered output | Host warnings point at generated C, and cccc passes `-w` today; `#line` mapping, or keep cccc's own `-W` checks for preserved attributes |
| OpenMP | Keep lowering onto cccc's runtime · preserve the pragma for `-fopenmp` |
