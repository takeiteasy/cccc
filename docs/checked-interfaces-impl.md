# Checked-Pointer Interfaces and Casts: Implementation

Contributor notes for [safety-checked-interfaces.md](safety-checked-interfaces.md): how call-argument checks, bounds casts and `dynamic_check` work. The passes they reuse are in [safety-impl.md](safety-impl.md).

## Call-argument checks

An annotated parameter is itself the bounds-safe interface (Checked C's `itype`); no marker is needed, because a checked attribute puts no restriction on an unchecked caller.

| Step | Where | What happens |
|---|---|---|
| Template | `resolve_type_checked_bounds()` / `resolve_param_checked_bounds()` (`src/parse_core.c`), where a prototype, typedef, function-pointer variable or struct member is declared | A prototype's bounds tokens have no body scope to resolve into. The same tokens resolve into a reusable **template**: a synthetic scope of placeholder `Obj`s, one per named parameter (`Obj.checked_self_param_idx`), pinned to **file scope** so a bound naming a global resolves against the globals declared before the declaration. Cached on the function type, so it runs once. A bad bound is an error at the declaration in every mode, and an unnamed pointer parameter resolves like a named one |
| Rewrite | `rewrite_checked_call_args()` (`src/parse_checked.c`) at each call | Every placeholder becomes a clone of that call's actual argument, and the qualifying argument `arg` becomes `(__ca = arg, __ca)`, where `__ca` is a fresh declared-checked local carrying the substituted template as its bounds |
| Check | The existing `verify_checked_assign_bounds()` walk | It finds `__ca = arg` and emits the usual `CHKAB` pair. No new opcode or codegen: this is the same shape `checked_cast_desugar()` produces for `assume` and `dynamic` |

A call is rewritten only when both hold:

| Condition | Reason |
|---|---|
| The argument is rooted at a declared-checked source (`find_checked_base()` + `checked_base_is_declared()`) or at a propagation candidate | A candidate's class (FULL, OPT, NONE) is only known once the body is parsed, so `__ca = arg` is built and the assignment scan verifies it only for a FULL source. An OPT one can hold the sentinel range, which `CHKAB` has no sentinel-aware variant for. An unchecked `int *raw` gets no check |
| Every *other* parameter the bounds name is fed a side-effect-free argument (`node_has_side_effects()`, the predicate bounds resolution already uses) | The template is re-evaluated, and an extra evaluation of a side-effecting expression is not worth a check. `sink(small, 8)` and `sink(small, len)` qualify; `sink(small, f())` and `sink(small, i++)` are silently declined, as `f()->p[i]` is for member bounds |

Other points:

- `[[cccc::single]]` needs no parameter substitution, so it always gets a caller-side check.
- A `_Checked[N]` argument decays to a declared-checked pointer before the rewrite runs, so it is covered.
- A prototype and a later definition with different bounds for one parameter are accepted silently: the first one parsed is the type every template resolves against.
- The pass is gated on `--checked-pointers` at parse time, like `verify_checked_assign_bounds()`. With the flag off `rewrite_checked_call_args()` never runs, so `-m`, `-c=native` and `-c=generated` output is unaffected.
- An annotated same-TU function's own body stays `CHKR`-checked against its declared bounds. This can newly trap code no unchecked caller ever touched, since the callee breaks its own contract. It uses its own declared bound, not the caller's storage, so an unchecked caller passing a too-short buffer does not trap there: closing that gap at the call boundary is the point of the feature.
- Not done: return-position bounds (`int * [[cccc::array, cccc::count(4)]] f(void);` parses and is unconsumed), arguments with side effects via pre-call hoisting, a warning when a check is declined, a prototype/definition disagreement diagnostic, propagated arguments, struct-field bounds interfaces, and an annotated libc/POSIX bounds table (which would make this useful for `memcpy` and `fread`). No checked-region call-boundary ban exists, so an annotated prototype was already callable from a checked region; `-Wchecked-calls` treats its checked parameters as crossing nothing.

## Bounds casts

`assume` and `dynamic` attach to a cast's type name (a compile error on a declarator) with the claimed checked kind and bounds. Both need a checked kind; `array` and `ntarray` also need a bounds form (`bounds(unknown)` is fine for `assume`, rejected for `dynamic`, which has nothing to verify against). They are mutually exclusive.

| Topic | Detail |
|---|---|
| Desugar | `(__cv = (T [[...]])expr, __cv)`, where `__cv` is a fresh compiler-generated local declared with the cast's kind and bounds. Every other pass sees an ordinary declared-checked local, so `CHKR` emission, propagation and `CHKAB` verification are reused unchanged |
| Direct dereference | `((T [[...]])q)[i]` with no assignment still finds `__cv`'s bounds through a narrow unwrap used only by the dereference-bounds attachment. It is deliberately hidden from propagation and `CHKAB`: those snapshot a source's bounds *before* the enclosing store, an ordering `__cv`'s own store (a side effect of the expression being snapshotted) would break |
| Gate | `--checked-pointers` at parse time; with it off the cast stays a plain cast and native output is unaffected |
| `dynamic` | `__cv = (T)expr` is exactly the `q = E` shape `verify_checked_assign_bounds()` recognises, so it reuses `CHKAB`. The source must be a declared-checked base with a resolvable bounds form (not `bounds(unknown)`), or a propagation candidate classified FULL, or the cast is a compile error, always on. A candidate is queued on the function (`Obj.checked_dynamic_pending`) and settled by `resolve_pending_dynamic_casts()` after propagation; with `--checked-pointers` off that function runs the classifier alone (`checked_prop_classify()`), which is why candidates are registered in every mode. NONE and OPT sources get distinct errors Falling back to `CHKB`/`CHKD` would silently check nothing for a stack or global source |
| Second check | Assigning the cast's result into another declared-checked variable can trigger a second, independent `CHKAB` pair from the assignment scan (`__cv`'s bounds against the target's). It is a harmless, tighter restatement of the first |
| `assume` | The assignment-scan pass skips `__cv = (T)expr` (`Obj.checked_cast_kind == CC_ASSUME` is tested before matching): no check of the claim. Every later access through `__cv` is still `CHKR`-checked |
| Checked regions | Both are exempt from the region's cast ban: the sanctioned conversion, with no `unchecked` wrapper |
| Scope | Only inside a function body: `vm->compiler.current_fn` must be non-NULL for `__cv` to have somewhere to live, so a file-scope initializer is a compile error. The desugar is an expression, so it works as a call argument or `return` value too |

## Return values

`declare_checked_return_var()` (`src/parse_decl.c`, once the parameters are in scope) gives a function whose return type is a checked pointer with a bounds form a hidden local `Obj.checked_ret_var`, so a bound naming a parameter resolves there and a body local can't shadow it. `rewrite_checked_return()` turns `return E` into `return (__cr = E, __cr)` when `E` roots in a declared-checked pointer or a propagation candidate, and the assignment scan emits the `CHKAB` pair as for `q = E`. Block literals infer their return type and have no such local.

## `dynamic_check`

`dynamic_check(cond)` is spelled `__builtin_cccc_dynamic_check(cond)` or `_Dynamic_check(cond)`, recognised by position (an identifier not in scope followed by `(`), so it never shadows a real function. Under `--checked-pointers` it becomes the `CHKDC` opcode (`__cccc_chkdc` in `-c=native` and `-m` output); otherwise it parses to a plain `ND_NULL_EXPR`, like `__builtin_assume`.

`cond` must be side-effect-free, as for `count()`, `byte_count()` and `bounds()`: it is only evaluated under the flag today, and a planned static-elision pass would treat `dynamic_check` facts as proven truths without evaluating them.

## Enforcement and output

The compile-time rules (attribute parsing, type checking, `single` arithmetic rejection) are always on; only the `CHKR`-family runtime checks are gated on the flag.

| Output | Behaviour |
|---|---|
| `-c=native`, `-m` | Each check calls a small helper emitted at the top (`__cccc_chkr`, `__cccc_chknt`, `__cccc_chkab`, `__cccc_chkdc`). A failure prints the VM's banner without the `PC:` line and exits 255. Without the flag nothing is emitted. Comptime code still runs in the VM, so its checks are enforced there under the flag |
| `-c=generated` | Emits only macro-generated code, so there are no user accesses to check: it warns `-c=generated ignores VM runtime safety/debug options` and drops the flag |
| Attributes | The `single`/`array`/`ntarray`/`count`/`byte_count`/`bounds` attributes are always stripped from `-E`, `-m`, `-c=generated` and `-c=native` output (ABI-transparent) |
| Frontend contract checks (`single` arithmetic, side-effecting bounds, member bounds naming a local or bit-field sibling) | Independent of the flag; apply in every mode |
