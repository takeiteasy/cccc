# Safety Implementation Notes

Contributor notes behind the [safety](safety.md) guides.

## Bounds propagation

- **Snapshot, not dataflow.** The range is two absolute addresses in hidden locals, written at each assignment, so there is no kill-set tracking and no CFG.
- **Candidates.** An unchecked pointer local is a candidate. Classification (NONE/FULL/OPT) covers the whole function; address-taking outside the temporary built for `q += k`, `q++` and `q--` disqualifies it.
- **OPT is per-path.** Every store to an OPT candidate refreshes its snapshot: a rooted store writes real bounds, any other store writes an inverted sentinel (`lo = -1`, `hi = 0`). A survivor that holds both plain arrays and checked values (`Obj.checked_prop_mixed`, also set by a chain from one) has a second pair, `checked_prop_src_lo/hi`, that records the plain array's range; dereferences read the first pair, `checked_prop_source_bounds(..., use_src_pair)` reads the second for checked targets. The temps are also seeded at function entry, which covers uninitialized declarations, loop back-edges and `goto` past the declaration. `CHKRO` treats the sentinel as a no-op; FULL candidates keep the plain `CHKR`, so their code is unchanged.
- **Chaining is a fixpoint.** Round 0 accepts declared-checked sources. Each round then also trusts the previous round's survivors. Seeding from below keeps an unrooted cycle from validating itself. The loop is capped, and any survivor still undecided at the cap is forced to OPT, the only sound direction since OPT only costs precision. OPT-ness threads through the chain, or a chained pointer would read its source's sentinel through a plain `CHKR` and trap on correct code.
- **Terminator guard.** `Obj.checked_prop_nt_elem` is non-zero when every rooted store comes from an `ntarray` with one agreed element size. The OPT sentinel no-ops `CHKNT` because its `hi` is 0, so no sentinel-aware variant exists.
- **Read-modify-write.** `to_assign()` desugars `q[n] += 1` at parse time, before bounds resolve. It leaves a back-link (`Node.checked_rmw_mirror`) from the dereference to the synthesized store, and the attach walk copies the resolved bounds onto it. The atomic desugar copies only hi, access size and terminator, because the compare-exchange node has no `lo`.

## Assignment-time check

`verify_checked_assign_bounds()` is a sibling pass over declared-checked targets, disjoint from propagation. It rewrites `q = E` into `(temp = source bounds), (q = E)` and emits `CHKAB rs_val, rs_slo, rs_shi` twice (target `lo`, then `hi`), which traps unless `slo <= val <= shi`. The source is snapshotted *before* the store because the store may overwrite it. The target's bounds are evaluated *after* the store because they refer to the target itself (`[q, q + m*sizeof(T))`). A propagated source is used through its snapshot temps. An OPT one may hold the `[-1, 0)` sentinel, so its assignment emits `CHKABO` instead, which no-ops on the sentinel the way `CHKRO` does (`CHKAB` and `CHKABO` share `chkab_common()`).

Both passes run at parse time only under `--checked-pointers`, because the snapshots cost stack slots. `#pragma cccc config(checked_pointers = true)` resolves during preprocessing, so its position in the file does not matter.

## Dangling pointers

Enforced in `CHKP3`, the gate before every dereference under `--pointer-checks` and `-3`. All layers run at dereference time; escape analysis only prunes redundant recording.

1. **Range.** The VM stack grows down, so live locals sit at `>= vm->sp`. An address in the stack reservation below `sp` belongs to a returned frame.
2. **Frame epoch.** `ENT3` gives each activation a monotonic epoch. `LEA3` tags `&local` with the current epoch in `stack_ptr_epochs`, the stack analogue of the heap's `ptr_tags`. A tag whose epoch is absent from `live_epochs` is dangling even when the address is above `sp` again. Because stack addresses are reused, a dead exact tag does not conclude dangling by itself: it falls through to layer 3, which reports only if no live interval covers the address.
3. **Interior intervals.** `&arr[i]` with a runtime `i` is a base `LEA3` plus a runtime `ADD`, so only the base is tagged. `STKTAG` after the base `LEA3` of an escaping array/struct local (and of every `vector_size` local, since element access re-derives its address) retains `[base, base+size)` with the frame's epoch in `vm->stack_intervals`. Intervals are kept rather than pruned on frame death, because dead and live extents can share addresses. `CHKP3` prefers the max-epoch *live* containing interval and falls back to the max-epoch dead one. Epoch order alone is unsound: an unrelated dead sibling can have a higher epoch than a live frame at the same addresses.

**Local aggregate access skips `CHKP3`.** A local struct's member access lowers through `gen_addr()` like a pointer dereference and could hit a stale tag left by a returned sibling. `addr_is_local_frame` (`src/codegen_addr.c`) recognises addresses built purely from the current function's bp-relative local offsets, with no pointer value load (captured variable, static-link chain, by-pointer aggregate parameter, `__block` wrapper, real dereference), and skips the check. A real dereference always bottoms out in a dereference and stays checked. Array and vector indexing and stack-spilled `va_arg` slots do not qualify, which is why layer 2 falls through to layer 3.

**Pruned recording.** `mark_addr_escapes` (`src/parse_analysis.c`) sets `Obj.addr_escapes` when a local's address (or an owned array or struct, interior-aware, or pointer arithmetic on its base) is a call argument, a `return` operand or stored to a pointer or aggregate lvalue. `++`, `--` and compound assignment on a plain local, a member or an element are not escapes. `LEA3_NO_RECORD` makes `op_LEA3_fn` skip the `stack_ptr_epochs` write for unmarked addresses. Unmarked defaults to recording, so an unrecognised pattern costs a hashmap entry and never a missed catch.

**Lazy per-function push.** `ENT3` pushes an epoch only when the body emits `STKTAG` or a recorded `LEA3`. `LEV3` and `CALLT` retire an epoch only when the top entry's saved `bp` matches the unwinding frame, so no per-frame flag is needed. Sparing use of `__builtin_dynamic_object_size` costs only the functions on the path to its buffer.

An earlier scope-exit check tracked every `&local` with a `MARKA` opcode and aborted at function exit, which confused "address taken" with "escaped" and flagged benign code. It was removed.

## Constant-index elision

`set_checked_deref_bounds()` (`src/parse_checked.c`) asks `checked_access_provably_in_bounds()` before building bounds. When it holds, the deref keeps `bounds_lo`/`bounds_hi` NULL, so the VM emits no `CHKR` and `-m`/`-c=native` emit a plain deref.

- The base must be an array that cannot be NULL: a variable, a member of a named object (`s.m`, not `sp->m`), or a row whose own access is checked or elided. `CHKR` also traps address 0, so pointer bases and array parameters keep it.
- The extent comes from the same resolved `count`/`byte_count` expression the check uses and must be constant. `bounds(lo, hi)`, `single`, VLAs and unresolved bounds do not qualify.
- Every `ND_ADD`/`ND_SUB`/`ND_CAST` between the deref and the base must carry a constant offset. `add_type()` wraps the integer operand in a cast to the pointer type, so the walk looks through it. `[0, extent)` is compared against the offset plus the larger of the accessed and element sizes. An `_Nt_checked` terminator slot is outside that range and keeps `CHKR`/`CHKNT`.
- `checked_prop_attach_scan()` re-attaches bounds to a bounds-less row element, so it runs the same predicate and skips what was elided.

## Dominated-check elision

`elide_checked_dominated_bounds()` (`src/parse_checked.c`) runs after loop-index elision in `checked_tail_passes()`. It sets `Node.checked_range_proven` on a deref whose `CHKR` an earlier check covers. The VM codegen (`gen_addr`, `ND_DEREF`) and the serializer (`checked_deref_lowered()`) skip only the range check on such a node; its bounds stay, so `CHKNT` still guards an `_Nt_checked` terminator slot.

- **Read-modify-write.** `to_assign()` checks `&A` and then the synthesized `*tmp` store (`rmw_mirror`). `mark_rmw_mirror()` marks the store when its original deref still has bounds of the same kind and size and the right-hand side passes `node_has_side_effects()`; otherwise the right-hand side could change what the bounds read between the two checks.
- **Flow.** `dominate_stmt()` walks the function's statements in order, threading a `Flow`: the cache of available checks plus a `dead` flag for a point no path reaches, which a join ignores. `flow_meet()` keeps a check only when both paths hold a covering one.
  - `if`: the condition's checks reach both arms; the join is the meet of the arms (no `else` is the post-condition state).
  - Loops: the head keeps the entry state minus everything `note_stmt_writes()` finds written in the condition, increment and body, and nothing when `body_has_side_entry()` finds a way in other than the top. One pass, so no fixpoint: checks made in the body never reach the head, and the exit is the post-condition state met with every `break`. `do { } while (0)` has no back edge.
  - `break`/`continue`/`return`: a `DomFrame` per loop and `switch` holds the meet of the states at the jumps to its labels. A jump whose target is not in a frame tells every frame nothing is known.
  - `switch`: the body starts dead; each `case` meets the post-condition state with the fall-through; without a `default` the exit also meets that state.
  - A label some `goto` or asm goto names restarts empty, and so does any statement kind not handled; an unnamed label passes the state through. A function holding an unexpanded macro call treats every label as named, because the splice can add a `goto`. A macro call in a controlling expression also tells every enclosing frame nothing is known at its `break`/`continue` targets (`flow_forget_frames()`), and `node_has_side_effects()` counts `ND_MACRO_CALL`, so the read-modify-write mirror is not elided across one.
  - A block with a `cleanup` variable kills memory-reading checks at its end, and at a jump out of it, because the handler is a call.
  - Statement lists nested in an expression (a statement-expression) start empty.
  - A function with label addresses or `setjmp` is skipped.
- **Matching.** `check_covers()` needs the same address, `bounds_lo` and `bounds_hi` (`check_key_equal()`, side-effect-free node kinds only), no larger `access_size`, and a plain earlier check for a plain access. Accesses with `bounds_obj_init` are left alone. Accesses of the same statement never cover each other, since their order is unspecified; only checks made under no `?:`, `&&` or `||` operand are added.
- **Invalidation.** `note_writes()` collects what a statement writes: variables (a struct member write counts as its variable), memory (a store through a pointer, any call) or everything (any node kind it does not know). Entries mentioning a written variable die, and entries that read memory die on a store or call. A statement's own entries are filtered the same way, so `x = p[i] + (n = 3)` leaves nothing behind.
- **Memory variables.** A variable counts as memory when it is global, `static`, captured, `__block` or has its own storage addressed (`note_addressed()`). `Obj.addr_taken` is not used because it also marks the pointer in `&p[i]`.

## Loop-index elision

`elide_checked_loop_bounds()` (`src/parse_checked.c`) runs last in `checked_tail_passes()`, after propagation and sibling-write grouping, because it needs the whole loop body. It finds counted `ND_FOR` loops and removes the bounds from the accesses their variable indexes.

- `loop_header()` accepts `i = L` (constant, `>= 0`) in the init, `i < N` or `i <= N` (constant `N`) as the whole condition, and an increment whose only write to `i` is `i = i + c` for a constant `c > 0`. `i` must be an `int`-width or wider local that is not `static`, `volatile`, atomic, `__block`, `addr_taken` or `is_captured`; with those, no call or `asm` can write it. A narrower variable does not qualify, because its increment goes through a narrowing cast. `hi + c` must fit the type.
- A loop whose condition, increment or body holds an unexpanded `ND_MACRO_CALL` does not qualify: `cc_expand_macros()` runs after this pass, so the call may write `i` or splice a jump.
- The body must not write `i` and must have no `case` outside a switch of its own and no `ND_LABEL` that a `goto` or asm goto names (`note_goto_target()`, by label name; every label when a macro call is pending). A function with `ND_LABEL_VAL`/`ND_GOTO_EXPR` or a `setjmp` call has no qualifying loop.
- `checked_access_provably_in_bounds()` takes a `LoopRange` chain and treats the offset as an interval: a constant is `[c, c]`, a loop variable is `[L, hi]`, and `+`, `-`, `*` and integer casts combine intervals, failing when an intermediate could leave its type. The check is `off_lo >= 0 && off_hi + size <= limit`.
- Only derefs inside the loop's `then` qualify. Nested loops extend the chain, so `grid[i][j]` sees both ranges.
- Accesses are collected first and stripped afterwards, since a row access is recognised as checked through the bounds of the row it indexes. `checked_drop_bounds()` clears the deref and its `rmw_mirror`, the store or `ND_CAS` copy a read-modify-write desugar made, so `a[i] += 1` loses both checks.

## Terminator guard

- `CHKNT` traps a store of a non-zero value into the widened slot, keyed off the `[lo, hi)` `CHKR` already computed (`addr >= hi - slot_size && val != 0`). `CHKNTZ` traps a non-all-zero-bytes store for pointees whose store never goes through one value register; it scans the source bytes before the `memcpy` runs, so the slot is never clobbered. Its second immediate is the scan size, which is smaller than the slot for a struct-typed member store.
- Floating-point values move into an integer register first (`FR2R`, `FR2R_F32`), so `-0.0` is non-zero. `long double` is unguarded because its slot is 16 bytes but the store is an 8-byte flat-double `FSTR`; vectors and `_Complex` have no opcode that reads their full representation.
- `s[n] += 1` and `s[n]++` desugar in `to_assign()` to `tmp = &s[n]; *tmp = *tmp + 1`, and the synthesized store carries the same `checked_bounds_lo/hi` and `checked_nt_terminator`. An `_Atomic` element takes the CAS-loop desugar and gets its own `CHKNT` ahead of `ACAS`, checking the desired value. `CHKNTZ` has no CAS counterpart because C has no compound assignment on aggregates.
- A member store (`tbl[n].a = 1`, `tbl[n].arr[k] = 1`) finds the element's `ND_DEREF` through `checked_nt_store_root()` (`src/parse_checked.c`), which walks `ND_MEMBER` chains, array-member subscripts and the `rmw_lvalue` link of a desugared read-modify-write. The guard fires for any store at or past `hi - slot_size`; the slot size is the element size, carried in `Node.checked_nt_slot` on a synthesized store whose own access size is the member's. The native `__cccc_chknt` takes the slot and scan sizes separately. A bit-field store passes `value & mask` to `CHKNT` and the native lowering guards a local copy of that masked value against the enclosing struct, since a bit-field has no address; a field wider than 64 bits is unguarded.
- Presence validation is not implemented: `count(n)` is a lower bound in Checked C, and locating the real terminator needs a read past `hi`.

## Type shadow

- A byte-granular shadow keyed by `(char*)ptr - vm->heap_seg`; `CHKT3` checks, stamps and clears. Allocations resolve through `vm->sorted_allocs`, the same binary search as `--bounds-checks`, so interior pointers work.
- Globals need no lifetime tracking, since `data_seg` is never reused; only the struct-return buffer pool is cleared on each hand-out. The stack is excluded because slot reuse would need liveness tracking to avoid the false positives that the dangling-pointer bookkeeping exists to prevent.
- The VM has no optimiser, so every dereference takes the ordinary `emit_load`/`emit_store` path and its `CHKT3`.
- **Host-call backstop.** Indirect calls (function pointer, `dlsym`) are covered too. `ffi_shadow_clear_extent` (`src/ops.c`) clears a heap address by allocation extent and a data-segment address from the pointer to the end of the segment. Classification tiers only ever reduce clearing relative to an unclassified name: read-only (no clear), bounded-write (clear the statically known extent through the receiving argument; other pointer arguments get the default clear unless the rule marks them read-only, as for `strto*` `nptr`), and the printf family (read-only except the output buffer, falling back to the default when the format is unreadable, non-literal, truncated or contains `%n`). Format strings are read directly from the guest pointer.
- **`qsort`.** It only permutes whole elements, so a uniform shadow pattern survives. The check runs before and, unconditionally, after the host call, because the comparator is guest code whose stores can stamp a stale position even on an already-cleared range. A non-uniform range clears exactly `[base, base+nmemb*size)`. `bsearch` writes nothing and is read-only.
- **Sparse page table.** Shadow pages (64 KiB) are allocated on first stamp and freed when a single clear zeroes one. Pages reaching zero through several partial clears are queued for an amortized sweep, rate-limited by the VM cycle counter. A missing page already means "no effective type", so freeing is unobservable. `--vm-profile` reports `shadow_sweeps`, `shadow_pages_swept` and `shadow_pages_live`.

## Uninitialized detection

`MARKI` marks a variable initialized after assignment and `CHKI` validates before a read, keyed by BP address plus offset. A scalar local with `Obj.addr_taken`, `is_captured` or `is_block_var` is exempt.

## Bounds checks

- `CHKB`/`CHKBN` validate a pointer value as `p + n` / `p - n` forms it and allow one-past-the-end, since `a[i]` desugars to `*(a+i)` and `CHKD` is the real dereference check. A pointer difference is a raw ptr-ptr subtraction, not a scaled offset, so it is not checked.
- `CHKD` precedes every load, store, struct/union copy and vector access. For `ALDR`/`ASTR`/`AXCHG`/`ACAS` it is a standalone instruction emitted ahead, which avoids the register-aliasing hazard in the atomics' operand words; `ACAS` gets two (a failed compare-exchange reads and writes through `expected`). Emission is unconditional under `CCCC_BOUNDS_CHECKS`: these addresses are pointer-valued expressions, so the `addr_is_local_frame` classifier would never apply.
- Interior pointers resolve through `vm->sorted_allocs`; a negative offset is rejected only once it steps before the resolved allocation's start.
- Two lookups per scalar heap access (`CHKB` and `CHKD`), on top of `CHKP3`'s own.
