# Refocus plan: a comptime-first compiler

**Status: open.** Direction agreed; no work started. Tracked by #2178 (label REWRITE).

cccc moves toward a lean C23 core with a strong comptime system on top, **one feature at a time on trunk**. Nothing is rewritten wholesale. The comptime hooks that features move onto are designed first and proven on a single pilot (methods) before anything else is ported.

## Direction

| | Today | Refocus |
|---|---|---|
| Main job | Run any C program in the VM, emulating gcc/clang | Parse, run comptime, lower to C for the host compiler |
| The VM | Full program runtime with broad POSIX emulation | Mainly the comptime engine; running whole programs stays |
| Extensions | Hand-written in the parser | Moved to comptime one by one, as hooks allow |
| Vendor attributes | Reimplemented or silently dropped | Understood when they change types; otherwise passed through |
| Strategy | — | Incremental on trunk: no branch, no freeze |

## Principles

- **Hooks before ports.** The typed hooks decide what can leave the parser, so their design is proven on one feature before a second one moves.
- **One feature per change.** Port it, keep its suites passing, delete the parser code it replaces. A hook design that turns out wrong costs one feature.
- **"The VM is for comptime" steers new work; it is not a deletion list.** No new POSIX, OpenCL or kernel emulation. Existing code and tests stay, so safety checks and the debugger keep working for programs that use host features ([VM scope](#vm-scope)).
- **Fail when comptime reaches it, don't emulate up front.** New executable builtins and library functions come from what comptime actually calls.

## Phases

| | Phase | Done when |
|---|---|---|
| P1 | Hooks design and methods pilot ([Hooks](#hooks)) | `[[cccc::method]]` receiver calls run on a comptime hook; the methods suites pass with the parser's method-call code deleted |
| P2 | Review | The hook design is revised from what P1 taught, before any other port |
| P3 | Port features in order ([Feature inventory](#feature-inventory)) | Each feature's suites pass on comptime |
| — | Independent work, any time | [Pass-through](#pass-through), layout fixes (#2177), persona probe; each ships alone |

Self-hosting (`ouroboros`) and the full suite stay green after every step; they are the parity check.

## Hooks

The pivotal open design. P1 settles it on methods, the plainest typed hook: `s.f(x)` where `f` is not a member.

### Pilot scope

| In P1 | Later |
|---|---|
| `[[cccc::method(T)]]` and `[[cccc::method(T, name)]]` registration | Static methods `T.f()` (needs a parser form, see below) |
| `s.f(x)` and `p->f(x)` rewritten to `f(&s, x)` / `f(p, x)` | Struct-body definitions (a syntax macro, #1701) |
| Diagnostics: unknown method, wrong receiver type | Overloadable methods, template methods, derive-generated methods |

### Questions P1 must answer

| Question | Starting proposal |
|---|---|
| When does a hook fire? | When member lookup on a struct or union finds no member named `f` |
| How is it registered? | Comptime registers a function for a key; the attribute form is sugar for it |
| Dispatch | A C-side registry keyed by (type, name); no registered key means no VM entry, so normal code pays nothing |
| What does the hook receive and return? | The receiver expression, the name and the arguments; it returns a replacement node, which the compiler type-checks |
| Re-type-checking | The returned node is type-checked like parsed code; a hook that returns another hooked construct is followed up to a depth limit |
| Conflicts | Two registrations for one key are an error at the second registration |
| Diagnostics | Errors in the returned node point at the original call |
| Handles | P1 runs on today's comptime API. `Node *` is already opaque to comptime, so a later switch to handles ([Comptime API v2](#comptime-api-v2)) keeps hook signatures |

### Parser forms

Some extensions are grammar problems, not type problems: a hook during type checking never sees code the parser rejected. The core parser accepts a fixed set of extended forms as **unresolved nodes**, which typed hooks resolve; an unclaimed one is an error.

| Form | Example | Issue |
|---|---|---|
| Type call | `T(args)` | `T(x);` with `T` a typedef is already a declaration; needs a disambiguation rule |
| Type member | `T.f(args)` | A parse error in C today |
| Named argument | `f(.name = v)` | A parse error in C today |
| Template instance | `Name<T>` | `a < b > c`; needs to know `Name` is a template (#1685) |

### Other extension layers

| Layer | Runs | Notes |
|---|---|---|
| Syntax macros (#1645, #1701, #1703, #1704) | During the parse, on tokens | A macro defined in the same file runs mid-parse, so comptime is interleaved with parsing, not a prior phase |
| Readers (#1702, #1705) | On raw text | Open: before or after preprocessing, and how the reader's text ends |
| Infix operators | During the expression parse, with a precedence | The file is tokenized before its own registration runs, so the parser matches adjacent punctuators (`<=` then `>` with no space) instead of retokenizing |

## Feature inventory

Ordered roughly by port order.

| Feature | Destination | Needs |
|---|---|---|
| Methods `s.f()` | Typed hook (pilot) | P1 |
| Operators on struct operands | Typed hook keyed by (type, operator) | Tests that P1 generalises |
| `a <=> b` | Infix operator hook | Infix layer |
| Static methods, `T(args)`, `f(.name = v)`, `@ctor`, `@default` | Typed hooks on parser forms | [Parser forms](#parser-forms) |
| Struct-body methods and operators, `@name` | Syntax macro to the free spelling | #1701 |
| Templates, `@specialize`, packs, deduction | Comptime with a core parser helper | Deduction needs a call-resolution hook; hardest, last |
| Checked pointers | First-party extension pack (`<cccc/checked.h>`) | A mature hook API; not a gate |
| Derive, `^^name`, quasi-quotes, `@test` | Already comptime | — |
| Overloading (`overloadable`, `enable_if`) | **Stays in the core** | The macOS SDK's `tgmath.h` and `simd/*.h` use it, so typing calls through host headers needs overload resolution |
| OpenMP | Lowered onto cccc's runtime today; preserving the pragma for `-fopenmp` is an option | Decision |
| Kernels, OpenCL | Frozen ([VM scope](#vm-scope)) | No host C compiler accepts them, so they cannot be passed through |
| `_Decimal*`, `_BitInt(N>128)`, the native shims and shared runtimes | Stay | — |
| Build system, REPL, watch, config, debugger | Stay | — |

## VM scope

The POSIX library (sockets, mqueue, termios, aio, …), OpenCL and kernel emulation are **frozen**: kept and tested, but no new work. Later they move to an **optional layer**, a separately built library of host functions or a `cccc-run` binary, so the core shrinks without losing them. The VM already registers host functions by name with capability classes, which is the seam.

Keep the core (preprocessor, parser, VM) behind a library boundary in this repository rather than a separate project: comptime hooks call into compiler tables, so the two stay tightly coupled.

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
| `__builtin_clz` | Understand (a type signature); execute because comptime uses it |
| `__builtin_ia32_*` | Understand only; "not available at compile time" if comptime reaches it |

A construct that changes a type, a layout or a constant value is never passed through unmodelled: if cccc does not model it, that is an error.[^mode]

## Pass-through

Independent of hooks. Today unknown attributes are dropped from lowered output: `-m` loses `hot`, `ms_abi`, `frobnicate(1, x)` and `[[vendor::thing]]`.

Unknown content is kept only in **opaque slots**, places where C23 and GNU syntax already wrap a balanced run of tokens:

| Slot | Example | Kept |
|---|---|---|
| GNU attribute | `__attribute__((ms_abi, foo(1, x)))` | Name and argument tokens |
| C23 attribute | `[[vendor::thing(a, b)]]` | Namespace, name, argument tokens |
| `__declspec` | `__declspec(dllexport)` | Name and argument tokens |
| Asm label | `int f(void) __asm__("_f$v2");` | The string |
| Pragma | `#pragma ...`, `_Pragma(...)` | The token line |

- Each item is a record (namespace, name, placement, location, raw argument tokens) in a new lazily allocated AST group, attached to its owning node.
- The serializer writes it back in the same place.
- Comptime can claim an attribute through `Attrs(n)`; an unclaimed one passes through. Lowering is silent; running in the VM warns once per name.
- Lowered output carries `_Static_assert` checks of `sizeof`/`offsetof` for every type reflection relied on, so an unknown layout attribute fails the host build instead of desyncing.

### Attribute concerns

`src/attr_registry.c` rows mix four concerns. Split them as attributes move:

| Concern | Target |
|---|---|
| Meaning | About 25 `cccc::` meanings in the core, only for understood constructs |
| Spelling | A small C table mapping `gnu::packed`, `__packed__`, `__declspec(align)` to those meanings |
| Persona (`__has_attribute`, `__has_builtin`, predefined macros) | Probed from the host compiler (`cc -dM -E` plus one batched test file), cached per compiler and version; a fallback table when no compiler is installed |
| Output | The serializer |

## Comptime API v2

**Decided, not scheduled.** Comptime reaches nodes, types, objects and tokens through index handles (bounds- and generation-checked, valid for the whole compile) and about 20 generic builtins (`Kind`, `Child`, `ChildCount`, `Field`, `Attrs`, builders, diagnostics), with a typed header in comptime C on top. Returned nodes are re-type-checked; no in-place mutation. It replaces today's 244 per-field builtins and can land beside them, then retire them. It waits until hooks show which accessors they need.

## Open questions

| Question | Options |
|---|---|
| `T(x);` disambiguation | Constructor call only in expression context · only when `T` has a registered hook |
| Diagnostics on lowered output | Host warnings point at generated C, and cccc passes `-w` today; `#line` mapping, or keep cccc's own `-W` checks for preserved attributes |
| OpenMP | Keep lowering onto cccc's runtime · preserve the pragma for `-fopenmp` |
| Backend plugin API (#1461) | Plugins see AST structs (layout becomes public ABI) · plugins use handles |
| `Field(n, FIELD_X)` numbering | AST schema file (X-macro) · hand-numbered append-only enum. Leaning: schema file |

[^mode]: Today `mode`, `transparent_union`, `may_alias`, `weak` and `alias` are recognized and ignored. `typedef int di __attribute__((mode(DI)))` gives `sizeof(di) == 4` under cccc and 8 under clang (#2177).
