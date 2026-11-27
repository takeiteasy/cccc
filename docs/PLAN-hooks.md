# Hooks design (P1: methods)

**Status: design agreed; work not started.** Three minor questions remain ([Decisions](#decisions)). Part of [PLAN.md](PLAN.md), phase P1; tracked by #2178.

A **typed hook** is a comptime function the parser calls when it meets a construct it cannot resolve and a registration matches it. The hook returns a node, which replaces the construct and is type-checked in place, exactly like parsed code. P1 builds the mechanism and moves `[[cccc::method]]` calls (`s.f(x)`, `p->f(x)`) onto it.

## What the code does today

| Finding | Where | Consequence |
|---|---|---|
| Methods are a C table keyed by (type, `'m'`, name) | `register_method`, `find_method` over `binding_head` | The dispatch registry already exists; hooks add a kind to it |
| Method calls resolve during the parse, with the receiver already typed | `method_call` at `.` / `->` in `postfix` | The hook site exists; `v.len() * 2.0f` is typed `float` immediately |
| The rewrite picks an overload, adapts the receiver (`&s` or `*p`), applies checked deref bounds, then defaults and keyed args through `funcall` | `method_call`, `pick_method` | A hook-built call must get all of this, or methods regress |
| Comptime already registers methods | `RegisterMethod` (used by `derive.h`) | Derive keeps working unchanged |
| Call-site macros are **deferred**: the call becomes `ND_MACRO_CALL` typed `long` until `cc_expand_macros` runs after the whole parse | `primary` in `parse_postfix.c` | A macro's result is not typed in context: `mac().x` or `mac() * 1.5f` cannot be checked during the parse |
| Builders duplicate the parser's semantics loosely: `MakeFuncCall` casts every argument to its parameter type, with no arity or conversion checks, defaults, keyed args or overloads | `__builtin_ast_funcall` | Code a hook builds is checked less strictly than the same code written by hand |
| Comptime can run mid-parse with the parser state saved | `cc_execute_attribute_macro`, `cc_expand_const_macro_calls` | Eager hook execution reuses an existing pattern |

## Proposal

### 1. Hooks run eagerly, during the parse

The expression around a method call needs its type at once (conversions, `.x` on the result, operators). So a hook runs at the call site, during the parse, not as a deferred `ND_MACRO_CALL`. Its result is type-checked immediately and the parse continues on it.

Constraint: the hook function and everything it calls are defined before the first call site, as for attribute macros today.

### 2. One checker for parsed and built code

Factor the checks in `funcall` (arity, argument conversions and their diagnostics, defaults, keyed args, overload selection, checked-pointer arguments) into one function the parser and `MakeFuncCall` both call. A hook-built call is then exactly as strict as a hand-written one. This is P1's first work item (P1a) and is useful on its own: it fixes the builder leniency for every comptime user.

Risk: comptime code that relied on the loose builder (`derive.h`, examples) may start failing. The suites will show where.

### 3. Registration

| Tier | Key | Runs | Used by |
|---|---|---|---|
| Binding | (type, name) → C function | No VM; the core's fixed rewrite | Today's methods |
| Hook | (type, name) → comptime function | The VM, once per call site | New in P1 |

Rules, kept from today's methods:

- A real member always wins over a binding or hook of the same name.
- A registration belongs to its type, not to a scope.
- Two registrations for one key are an error, except overloadable functions.
- Speculative parses that roll back registrations (the restore loops in `parse_expr.c`) roll back hooks too.

### 4. Hook signature

```c
@comptime
Node *len_hook(Node *recv, ...);  // recv: the receiver as a struct lvalue
                                  // the call's arguments: VarargAt(i)
```

- `recv` is always an lvalue of the struct type: for `p->f()` it is `*p`. A hook that wants a pointer takes its address, and the core folds `&*p` back to `p`.
- Arguments arrive parsed and unconverted, as call-site macro arguments do today.
- The hook returns an expression node; `NULL` is an error at the call site.
- Errors the checker raises while the hook builds its result point at the call site (`macro_call_tok`).

### 5. No re-triggering

Hooks fire only at parse sites. Builders produce resolved nodes, so a hook's result never contains another unresolved member call, and no recursion limit is needed in P1.

### 6. Methods on the hook

`[[cccc::method(T)]]` becomes a registration of a hook, written in comptime C, that builds `fn(&recv, args...)` through the shared checker. The checker's overload selection replaces `pick_method`. The attribute itself is still parsed in C in P1, because comptime claiming attributes is separate work ([Pass-through](PLAN.md#pass-through)).

## Work items

| | Item | Done when |
|---|---|---|
| P1a | Shared call checker; `MakeFuncCall` uses it | Every suite passes; a new suite checks that builder calls get arity, conversion and keyed-arg errors |
| P1b | Eager hook execution: call a comptime function mid-parse with the parser state saved | A test hook runs at a member call and its result is typed in context |
| P1c | Hook tier in the binding registry; a comptime builtin to register a hook | Registration, conflict and rollback tests |
| P1d | `[[cccc::method]]` on the hook; delete `pick_method` and the receiver adaptation | The 7 methods suites pass, self-hosting passes, benchmarks re-run |

What P1 does **not** move: static methods `T.f()` ([parser forms](PLAN.md#parser-forms)), struct-body methods (syntax macro), overloadable and template methods beyond what the checker gives for free, and attribute claiming. P1 deletes little code; its value is proving the mechanism before operators reuse it.

## Decisions

**Decided:**

| Question | Choice |
|---|---|
| When hooks run | Eagerly in the parse; deferred results cannot be typed in context |
| Builder strictness | Shared checker first (P1a) |
| Where methods end up | A comptime hook; the binding tier stays as a fallback if benchmarks demand it |
| How the receiver arrives | Always a struct lvalue (`*p` for `p->f()`) |

**Open:**

| Question | Options | Leaning |
|---|---|---|
| How a hook is named at registration | By function name string · by a `^^fn` reflection handle | `^^fn`: checked at registration, no lookup by string |
| Wildcard `(T, *)` hooks for any unknown member | In P1 · later | Later: no P1 feature needs it |
| Property access `s.f` without a call | In P1 · later | Later; an error until then, as today |

## Speed

Lookup stays in C: a type with no hook pays nothing. A hooked call costs one VM entry at compile time per call site. P1d re-runs `bench_compare` and reports the methods-heavy cases; if the cost matters, methods fall back to the binding tier without changing the hook API.
