# Checked-Pointer Interfaces and Casts: Implementation

Contributor notes for [safety-checked-interfaces.md](safety-checked-interfaces.md): how call-argument checks, bounds casts and `dynamic_check` work. The passes they reuse are in [safety-impl.md](safety-impl.md).

## Call-argument checks

An annotated parameter is itself the bounds-safe interface (Checked C's `itype`); no marker is needed, because a checked attribute puts no restriction on an unchecked caller.

| Step | Where | What happens |
|---|---|---|
| Template | `resolve_type_checked_bounds()` / `resolve_param_checked_bounds()` (`src/parse_core.c`), where a prototype, typedef, function-pointer variable or struct member is declared | A prototype's bounds tokens have no body scope to resolve into. The same tokens resolve into a reusable **template**: a synthetic scope of placeholder `Obj`s, one per named parameter (`Obj.checked_self_param_idx`), pinned to **file scope** so a bound naming a global resolves against the globals declared before the declaration. Cached on the function type, so it runs once. A bad bound is an error at the declaration in every mode, and an unnamed pointer parameter resolves like a named one |
| Rewrite | `rewrite_checked_call_args()` (`src/parse_checked.c`) at each call | Every placeholder becomes a clone of that call's actual argument, and the qualifying argument `arg` becomes `(__ca = arg, __ca)`, where `__ca` is a fresh declared-checked local carrying the substituted template as its bounds |
| Check | The existing `verify_checked_assign_bounds()` walk | It finds `__ca = arg` and emits the usual `CHKAB` pair. No new opcode or codegen: this is the same shape `checked_cast_desugar()` produces for `assume` and `dynamic` |

A call is rewritten only when the argument is rooted as below. A side-effecting sibling argument the bounds name is hoisted first:

| Condition | Reason |
|---|---|
| The argument is rooted at a declared-checked source (`find_checked_base()` + `checked_base_is_declared()`) or at a propagation candidate | A candidate's class (FULL, OPT, NONE) is only known once the body is parsed, so `__ca = arg` is built and the assignment scan verifies it only for a FULL source. An OPT one can hold the sentinel range, which `CHKAB` has no sentinel-aware variant for. An unchecked `int *raw` gets no check |
| Every *other* parameter the bounds name is fed an argument that is side-effect-free (`node_has_side_effects()`) or can be hoisted | The template substitutes a clone of that argument, so one with side effects is first stored in a temp. `sink(small, 8)`, `sink(small, len)`, `sink(small, f())` and `sink(small, i++)` are all checked. A sibling that is itself a verified checked argument keeps the call unchecked |

The hoist is a prelude comma around the whole call, `(__t = f(), sink(...))`, flagged `Node.call_prelude`. Hoisted siblings are evaluated once, in argument order, before every argument that stays in place, because call arguments are unsequenced and a check in one argument cannot read a temp stored in another. `checked_call_root()` returns the prelude comma and `split_checked_call_result()` wraps it, so a checked-return call keeps its return bounds. A `return` of such a call is not a tail call.

Other points:

- `[[cccc::single]]` needs no parameter substitution, so it always gets a caller-side check.
- A `_Checked[N]` argument decays to a declared-checked pointer before the rewrite runs, so it is covered.
- A row of a multidimensional checked array (`a[1]` of `_Checked[3][5]`) is a third root kind: `find_checked_base()` returns `.row` for an `ND_DEREF` of checked array type, with the row expression in `.obj` and its extent from `Type.checked_array_extent`. It takes the member-root path (side-effect decline, `&row` hoist), so `f(a[1])` is covered.
- A variable-length checked array keeps `Type.checked_array_extent` at 0 and records the extent's first token in `Type.checked_vla_extent_tok`. A local's bound is the `vla_len` snapshot `compute_vla_size()` stores; a parameter decays to a pointer whose `count(n)` re-parses that token span in the body scope, so `rewrite_checked_call_args()` covers it like any `count(n)` parameter.
- `merge_checked_interface()` (`src/parse_core.c`) runs at each redeclaration in `function()` and `declare_function_prototype()` (`src/parse_decl.c`). It compares kind, bounds form, `checked_array_extent` and the resolved templates (`checked_tmpl_equal()`, sibling placeholders by `checked_self_param_idx`) of the return value, each parameter and, recursively, function-pointer parameters, and errors on a mismatch. Bounds only the new declaration spells are copied onto a `copy_type()` of `fn->ty`, because a typedef may share the original.
- The pass is gated on `--checked-pointers` at parse time, like `verify_checked_assign_bounds()`. With the flag off `rewrite_checked_call_args()` never runs, so `-m`, `-c=native` and `-c=generated` output is unaffected.
- An annotated same-TU function's own body stays `CHKR`-checked against its declared bounds. This can newly trap code no unchecked caller ever touched, since the callee breaks its own contract. It uses its own declared bound, not the caller's storage, so an unchecked caller passing a too-short buffer does not trap there: closing that gap at the call boundary is the point of the feature.
- Not done: a warning when a check is declined, propagated arguments, and an annotated libc/POSIX bounds table (which would make this useful for `memcpy` and `fread`). No checked-region call-boundary ban exists, so an annotated prototype was already callable from a checked region; `-Wchecked-calls` treats its checked parameters as crossing nothing.

## Standard library bounds

`apply_checked_libc_bounds()` (`src/checked_libc.c`) runs after a function declaration or definition is recorded, in `declare_function_prototype()` and `function()` (`src/parse_decl.c`), under `--checked-pointers`. A row of its table is a full annotated prototype; the hook tokenizes and parses it with `declspec()`/`declarator()`, so the bounds reach a template the usual way, and merges it with `merge_checked_interface_defaults()` (`src/parse_core.c`).

| Rule | Reason |
|---|---|
| Only a non-`static` declaration in a file with `is_system_header` set | A program's own `read` is never touched. A quoted include inside a system header is a system header too, so the bundled `sys/socket.h` → `"unistd.h"` chain is covered when it resolves from disk |
| Only pointer parameters with neither a kind nor bounds are filled | A program's annotation wins. Return values are never filled |
| Parameter count must match the row | A same-named function of another shape is skipped silently |
| The merge reports no conflict | The table is a default, not a declaration |
| `Obj.checked_libc_mask` records the filled parameters | A later declaration that spells bounds for one of them clears it and takes over, where a plain redeclaration would be a conflict |

The same hook covers the hidden builtins (`memcpy`, `memset`, ...): `adopt_hidden_builtin()` skips `merge_checked_interface()`, and the table runs after either branch.

## Plain array sources

`checked_plain_array_root()` (`src/parse_checked.c`) is the pure predicate: `find_checked_base()` roots at a complete `TY_ARRAY` variable (a string literal is one), or at a non-bit-field array member of a struct that is not its last member, reached through a trivial object expression. The last member is excluded because it may stand in for a flexible array. `checked_plain_array_source()` builds the bounds `[decay(arr), decay(arr) + size)`.

| Consumer | Change |
|---|---|
| `verify_checked_assign_scan()` | Falls back to the predicate when `checked_prop_source_bounds()` finds no source, in both the probe and the committed rewrite |
| `checked_param_qualifies()`, `rewrite_checked_return()` | Accept a plain-array root, so the `__ca`/`__cr` store reaches the scan |
| `check_checked_cast_dynamic_source()` | Accepts it, so `__cv = (T)arr` reaches the scan |

`checked_prop_source_bounds()` and the propagation passes do not see plain arrays, so an unchecked `int *q = buf;` keeps no bounds.

## Struct fields

| Site | Where |
|---|---|
| Assignment `s.p = E` | `verify_checked_assign_scan()` already takes an `ND_MEMBER` target |
| Local aggregate initializer | The initializer lowers to a comma chain of member stores, so each pointer store is an ordinary stamped assignment |
| Sibling written after the pointer | `group_checked_sibling_writes()`, below |
| File-scope initializer | `check_static_checked_init()`, below |

### Group check

`group_checked_sibling_writes()` runs from `checked_tail_passes()` after `verify_checked_assign_bounds()`. It flattens each block's statement list (declaration groups are transparent) and each comma chain into leaves in evaluation order. A stamped assignment to a named variable or member whose bounds name siblings is an **anchor**; `group_process()` scans the leaves after it:

| Leaf | Effect |
|---|---|
| A store to a compiler temp with a pure value | Transparent; a `tmp = &obj` store records the object a compound assignment writes through |
| A write to a sibling of the anchor, on the same object (`nodes_structurally_equal()`), with a pure value that does not read the anchor's pointer | Extends the group |
| Another anchor on the same object with a shared sibling | Joins the group |
| Anything else | Ends the group |

With at least one sibling write, `group_move_stamp()` moves each anchor's five `checked_assign_*` fields onto a carrier `__cg = 0` wrapped after the last sibling write (`group_wrap_leaf()` rewrites that leaf in place into a comma). The source snapshot stays ahead of the pointer store. Codegen (`src/codegen_expr.c`) and `serialize_checked_assign()` read the stamp off any `ND_ASSIGN`, so `-c=native` needs no change. The last leaf of an expression-level comma chain never takes a carrier, since it carries the chain's value.

### Constant initializers

`check_static_checked_init()` runs after `write_gvar_data()` in `gvar_initializer()` and `cc_finalize_macro_gvar_inits()`, under `--checked-pointers`. It walks the `Initializer` tree like `write_gvar_data()` and checks each checked-pointer slot with an expression: `objsize_peel_offset_chain()` yields the array and byte offset, and the bounds template is cloned with each sibling placeholder replaced by that sibling's initializer expression and evaluated with `eval()`. `count`, `byte_count`, `ntarray` (one extra element) and `single` are handled; a non-constant bound, a null source or `bounds(lo, hi)` is skipped.

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

The bounds a `return` is checked against name the parameters' **entry** values: `declare_checked_return_var()` points each parameter reference at a hidden local (`Obj.checked_ret_entry` holds the `__rp = param` stores, prepended to the body by `checked_tail_passes()`), so a callee that reassigns a parameter is still checked against the value callers saw.

### Call sites

| Step | Where | What happens |
|---|---|---|
| Template | `resolve_param_checked_bounds()` (`src/parse_core.c`) | The return type's bounds resolve into `Type.checked_ret_bounds_lo/hi` on the **function** type, with the same parameter placeholders as argument templates. They are not stored on the return type, which a typedef can share between functions whose parameters sit at different indices |
| Recognise | `checked_call_root()` (`src/parse_checked.c`) | A pure predicate, the call analogue of `find_checked_base()`: `+`, `-` and casts down to an `ND_FUNCALL` whose callee returns a checked pointer and whose template names only scalar parameters it is passed. It allocates nothing and ignores `--checked-pointers`, so a `dynamic` cast from a call is accepted in every mode |
| Split | `split_checked_call_result()` | The call becomes a read of a declared-checked local `__cr` (`Obj.checked_call_result`) and the returned store is `__cr = call`. Each argument the template names is rewritten to `(__ak = ak)` and the bounds read `__ak`, so every argument is evaluated once and the bounds hold what the callee received |

The split is lazy, done by each consumer so a call that is not used as a checked source keeps its `ND_FUNCALL` (nodiscard, null-return analysis, serialization and tail calls see it unchanged). A direct dereference splits at parse time (`set_checked_deref_bounds()`, the comma is recognised by `comma_yields_checked_var()`). Propagation (`checked_prop_rewrite_assign()`) and assignment verification (`verify_checked_assign_scan()`) split in the tail passes and put the store ahead of the source-bounds snapshot, which reads `__cr` after the call. Annotated arguments, checked returns and `dynamic` casts only need the recognition: they build `__ca = call`, `__cr = call` and `__cv = (T)call`, which the assignment scan splits.

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
