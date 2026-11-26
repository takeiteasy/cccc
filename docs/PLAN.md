# Redesign plan: a comptime-first core

**Status: open.** Direction agreed, several decisions still undecided (see [Open questions](#open-questions)). No work has started. Tracked by #2178 (label REWRITE).

cccc becomes a lean C23 core — preprocessor, parser, type checker, VM — with a strong comptime and macro system on top. Most of what is built into the frontend today (vendor attributes, template syntax, methods, operators) is rebuilt as comptime code, syntax macros and readers, or simply carried through to the output untouched.

## Direction

| | Today | Redesign |
|---|---|---|
| Main job | Run any C program in the VM, emulating gcc/clang | Parse, run comptime, lower to C for the host compiler |
| The VM | Full program runtime with broad POSIX emulation | The comptime engine; running whole programs stays, as a secondary mode |
| Extensions | Hand-written in the frontend, one feature at a time | Comptime code, syntax macros and readers; "bring your own" |
| Vendor attributes | Reimplemented or silently ignored | Understood only when they change types; otherwise passed through |
| Backends | C serializer built in | Host C plugin API (`libcccc`); lowered C and Anvil are plugins |

**Running in the VM** (`cccc prog.c`) stays, but it is no longer the focus. It covers pure C with the bundled headers. A program that needs host-specific features is lowered instead.

## Execute, understand, preserve

Every construct gets one of three roles. Each role is a promise cccc makes about it:

| Role | Promise | Applies to |
|---|---|---|
| **Execute** | The VM runs it | Comptime code, and user code that comptime calls |
| **Understand** | Types, layout and constant values are exact | Anything reflection, type checking or lowering depends on |
| **Preserve** | Kept as written and passed through into the output | Everything else; the host compiler deals with it |

Examples:

| Construct | Role |
|---|---|
| `__attribute__((packed))`, `mode(DI)`, `vector_size` | Understand: they change `sizeof`/`offsetof`, which reflection exposes |
| `__attribute__((format(printf, 1, 2)))`, `nonnull` | Preserve: the host compiler warns about them on the lowered output |
| `hot`, `cold`, `target`, `flatten` | Preserve |
| `#pragma omp parallel for` | Preserve, and lower for the host compiler with `-fopenmp` |
| `__builtin_clz` | Understand (a type signature) always; execute because comptime uses it |
| `__builtin_ia32_*` | Understand only. If comptime reaches it: "not available at compile time" |

Two rules:

- **Semantic constructs must be understood.** A construct that changes a type, a layout or a constant value is never passed through unmodelled; if cccc does not model it, that is an error, not a silent pass-through.[^mode]
- **Fail when comptime reaches it, don't emulate up front.** The set of executable builtins and library functions grows from what comptime actually calls.

## Core scope

| In the core | Out of the core |
|---|---|
| Preprocessor | Vendor attribute meanings that don't affect types |
| C23 parser, type checker, layout | Template, method, operator and constructor syntax |
| VM and bytecode | OpenMP, OpenCL and kernel emulation in the VM |
| Comptime, reflection, quasi-quotes, `^^name` | The broad VM POSIX library (sockets, mqueue, termios, aio, …) |
| Sandbox and comptime policy | Hand-written `--output-compiler` persona tables (replaced by a host probe) |
| Runtime safety checks and the debugger, with the VM, switched on by flag | |

Safety checks still matter: they stop bad comptime code from crashing the compiler. The debugger and REPL become comptime tools as much as program tools: step through a macro, explore reflection live.

## Two header worlds

| World | Headers | cccc's job |
|---|---|---|
| **Comptime** | cccc's bundled headers, cleaned down to ISO C23 plus a few required builtins | Execute everything |
| **Target** | The host's system headers | Parse for types, preserve everything else, never run |

The bundled headers are already almost clean: 12 of 89 use an extension, mostly droppable hints (`format`, `alloc_size`) and builtins that C11 `_Generic` or core intrinsics cover.

The target world needs a parser that tolerates GNU syntax and keeps what it does not understand attached to the AST, so the output can write it back out. That is a parsing and AST concern, not a semantic layer ([Pass-through](#pass-through)).

## Pass-through

**Decided.** The parser does not accept arbitrary tokens. It parses C, the same inside and outside comptime code, and keeps unknown content only in **opaque slots**: places where C23 and GNU syntax already wrap a balanced run of tokens.

| Slot | Example | Kept |
|---|---|---|
| GNU attribute | `__attribute__((ms_abi, foo(1, x)))` | Name and argument token tree |
| C23 attribute | `[[vendor::thing(a, b)]]` | Namespace, name, argument tokens |
| `__declspec` | `__declspec(dllexport)` | Name and argument tokens |
| Asm label | `int f(void) __asm__("_f$v2");` | The string |
| Pragma | `#pragma omp parallel for`, `_Pragma(...)` | The token line, as a statement or declaration |

Outside the slots, code must be C or the fixed GNU syntax the core understands, because each affects types:

- `__typeof__`, statement expressions, `__extension__`, `__builtin_va_list`
- alternate keyword spellings (`__restrict`, `__inline__`, `__signed__`, …)
- extended `asm` operands, which are expressions over program variables

Anything else is a syntax error.

How it works:

- **Each item is a record:** namespace, name, placement and source location, with the arguments kept as raw tokens. A pragma is a raw token line.
- **Placement is recorded.** Each opaque item is attached to its owning node with where it was written (declaration, declarator, after the parameter list, type), and the C backend writes it back in the same place, unchanged.
- **Unknown builtins** are expressions, so they need a type signature (from data). One without a signature is an error where its type is needed.
- **Extensions can claim them.** `Attrs(n)` gives comptime the name and argument tokens as token handles. An unclaimed attribute passes through; a comptime extension can claim it and give it a meaning.
- **Unknown attributes all pass through.** Lowering is silent, since the host compiler warns about attributes it does not know. Running in the VM warns once per name.
- **Layout is checked.** Lowered output carries `_Static_assert` checks of `sizeof` and `offsetof` for every type whose layout reflection or codegen relied on, so an unknown attribute that changes layout (`ms_struct`, `scalar_storage_order`) fails the host build instead of silently desyncing.

## Extension layers

| Layer | Runs | Carries | Tickets |
|---|---|---|---|
| Syntax macros | Before the parse, tokens in, tokens out | `@name`, struct-body methods and operators rewritten to their free spelling, `unless`-style keywords | #1645, #1701, #1703, #1704 |
| Readers | Before the parse, raw text in | Foreign syntax: SQL, Lisp, format strings | #1702, #1705 |
| Infix operators | During the expression parse, with precedence | `<=>` and user operators, with typed operands | — |
| Typed comptime hooks | During type checking | `T(args)`, `T.f()`, `f(.name = v)`, overloading, derive, checked pointers | — |
| Host C plugin API (`libcccc`) | In the host, after type checking | Backends: lowered C, Anvil; embedding | #1461 |

Infix operators can't be syntax macros: a macro that runs before the parse cannot find where the left operand starts (`a + b <=> c`). They are their own hook, registered with a precedence, and the comptime function receives typed operands. `<=>` already tokenizes as `<=` `>`, so the tokenizer only accepts registered punctuator sequences.

```c
@comptime(infix("<=>", RELATIONAL))
Node *cmp3(Node *lhs, Node *rhs) { ... }
```

Syntax macros can't see types, so the typed hooks are the real core design work. Each feature rebuilt on top of them adds only the hook it needs, so the hook API grows from use rather than being guessed up front.

Backends are host C plugins, not comptime code: the C serializer alone is about 20k lines, and the plugin API doubles as the embedding API.

## Comptime access to the AST

**Decided.** Comptime reaches nodes, types, objects and tokens through **index handles** and a **small generic API**. The AST layout stays private to the compiler.

| Decision | Choice |
|---|---|
| Handles | Indices into the compiler's tables, checked by bounds and a generation tag; not host pointers |
| Lifetime | Valid for the whole compile, so registries and caches can span macros |
| Core API | About 20 builtins: `Kind`, `Child(n, i)`, `ChildCount`, `Field(n, FIELD_X)`, `Attrs(n)`, the builders, and diagnostics |
| Typed API | A header written in comptime C on top (`BinaryLhs(n)`, `StructMembers(t)`, …) |
| Changes | Build new nodes and return them; the compiler re-type-checks what comes back. No in-place mutation |
| Tokens | Syntax macros' `Tokens *` (#1701) use the same handle scheme |

```c
// core builtins (in the compiler)       // typed header (comptime C)
int   Kind(Node *n);                     static inline Node *BinaryLhs(Node *n) {
Node *Child(Node *n, int i);                 return Child(n, 0);
long  Field(Node *n, int field);         }
```

Today's API has one builtin per field (244, called through libffi with a hash lookup per handle) and reads host pointers. The new design keeps the safety, shrinks the core API, and leaves a fast path for later: the VM can dispatch the accessors itself, without libffi or the hash lookup, and the API does not change.

## Vendor attributes and builtins

Today each row of `src/attr_registry.c` mixes four concerns. The redesign separates them:

| Concern | Today | Redesign |
|---|---|---|
| **Meaning**: what it does | `attr_*` handlers in `parse_types.c` | About 25 cccc-named meanings in the core (`cccc::packed`, `cccc::mode`, `cccc::cleanup`, …), only for constructs that must be understood |
| **Spelling**: which names reach it | Namespace masks, `gnu_name`, `msvc_name`, six row macros | A small C table in the core, only for the understood meanings: `gnu::packed`, `__packed__`, `__declspec(align)` → `cccc::packed` |
| **Persona**: what `__has_attribute`, `__has_builtin` and the predefined macros answer | Hand-written `cc` masks checked against gcc and clang | Probed from the host compiler when lowering, cached per compiler path and version; cccc's own answers when running in the VM |
| **Output**: how it's written back out | `ATTR_OUT_*` flags | The backend plugin's job |

Everything outside the meanings is preserved as opaque data ([Pass-through](#pass-through)). The spelling table stays in C so that parsing system headers never depends on running comptime first; user extensions register new attributes through comptime.

The persona probe is `cc -dM -E` for the predefined macros plus one batched file of `__has_attribute`/`__has_builtin` tests. Cross-compiling probes the target compiler.

Builtins split the same way: every builtin gets a type signature, so host headers parse. Only a small set of core intrinsics also executes (`va_*`, `offsetof`, overflow arithmetic, bit counts, `expect`, `unreachable`, `trap`, `alloca`, `types_compatible_p`, C11 atomics). `__builtin_memcpy`, the libm aliases and the `_chk` family become aliases; `__atomic_*` and `__sync_*` are written over the core atomics.

## Checked pointers

**Decided:** not in the core. Checked pointers become a first-party extension pack, shipped with cccc, opted into (for example `#include <cccc/checked.h>`), and built only on the public hooks.

| Part | Built with |
|---|---|
| `_Ptr<T>`, `_Array_ptr<T>`, bounds declarations | Typed hooks on types and declarations |
| Checked/unchecked conversion rules, checked regions | Typed hooks during type checking |
| Runtime bounds checks | Ordinary C inserted into the returned AST (`if (i >= n) __builtin_trap();`), so they work in the VM and in lowered C alike |

The extension is not a gate for the switch-over (M5); it can land after.

Today's implementation is about 6.2k lines in the frontend. Once it works as an extension, the hook API is general enough for anything.

## Feature inventory

| Feature | Destination |
|---|---|
| `@name(args)` | Syntax macro |
| Methods and operators written in a struct body | Syntax macro to the free spelling (`[[cccc::method(T)]]`, `[[cccc::operator(+)]]`) |
| `a <=> b` | Infix operator hook |
| `T(args)`, `T.f(args)`, `f(.name = v)` | Typed hook |
| `Name<T>`, `f<T>(...)` | Core parser helper (#1685), templates themselves on comptime |
| Derive (`@serialize`, `@to_json`, …) | Comptime; already lives in `derive.h` |
| `^^name`, quasi-quotes | Core comptime |
| Checked pointers (`_Ptr<T>`, bounds) | First-party extension pack on typed hooks; the acceptance test for the hook API ([Checked pointers](#checked-pointers)) |
| Overloading (`overloadable`, `enable_if`) | Preserve when the host is clang, otherwise a comptime feature |
| OpenMP, OpenCL, kernels | Preserve and lower for the host compiler; drop the VM emulation |
| Testing (`@test`) | Comptime-generated; run in the VM or lowered |
| Build system (`--build`) | Stays: C run in the VM is exactly comptime's job |
| REPL, watch, config | Stay, as tools around the core |
| Lowering to C | Backend plugin |
| Anvil backend | Backend plugin (#1461) |

## Strategy

**A fresh branch, `rewrite`**, not an incremental migration. The parity target is small once most vendor features are *preserve*, and the AST must be designed from the start to round-trip what it does not understand. Retrofitting that into the current AST is the glue work this redesign exists to stop.

Guards against a stalled second system:

- **Trunk is frozen to bug fixes** while the branch is in flight.
- **Reuse what isn't bloated:** the VM interpreter and opcodes, most of the preprocessor, arena/hashmap/string utilities, the bundled headers, and the C library pieces comptime needs. Rewrite the parser, type checker and comptime API.
- **Port tests by category:** pure C and comptime suites move over unchanged and check correctness. Vendor-emulation suites are retired or become round-trip tests: the source parses, lowers, and the host compiler accepts the output.
- **Speed is not a goal yet.** Comptime is slow (#1418), and that can wait until the foundation works. The architecture that can't be fixed later, how comptime reaches the AST, is settled with a fast path in mind ([Comptime access to the AST](#comptime-access-to-the-ast)).

## Milestones

| | Milestone | Done when |
|---|---|---|
| M0 | Core runs pure C | The pure-C suites pass on the VM with the bundled headers |
| M1 | Comptime API v2 | Reflection and call-site macros work on the new AST; comptime suites pass |
| M2 | Lowering with pass-through | Host system headers parse; lowered output compiles with the host compiler, attributes intact |
| M3 | Syntax macros and readers | #1701–1703 land on the new core |
| M4 | `syntax.md` rebuilt | Each feature in [syntax.md](syntax.md) works as comptime code; its suites pass |
| M5 | Switch-over | The branch replaces trunk |

## Open questions

| Question | Options |
|---|---|
| `Field(n, FIELD_X)` numbering | One AST schema file (X-macro) generating field ids, the typed header and the dump · hand-numbered append-only enum. Leaning: schema file |

[^mode]: Today `mode`, `transparent_union`, `may_alias`, `weak` and `alias` are recognized and ignored. `typedef int di __attribute__((mode(DI)))` gives `sizeof(di) == 4` under cccc and 8 under clang (#2177).
