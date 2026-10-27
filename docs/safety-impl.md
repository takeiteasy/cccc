# Safety Implementation Notes

Contributor notes behind the [safety](safety.md) guides.

## Bounds propagation

- **Snapshot, not dataflow.** The range is two absolute addresses in hidden locals, written at each assignment, so there is no kill-set tracking and no CFG.
- **Candidates.** An unchecked pointer local is a candidate. Classification (NONE/FULL/OPT) covers the whole function; address-taking outside the temporary built for `q += k`, `q++` and `q--` disqualifies it.
- **OPT is per-path.** Every store to an OPT candidate refreshes its snapshot: a rooted store writes real bounds, any other store writes an inverted sentinel (`lo = -1`, `hi = 0`). The temps are also seeded at function entry, which covers uninitialized declarations, loop back-edges and `goto` past the declaration. `CHKRO` treats the sentinel as a no-op; FULL candidates keep the plain `CHKR`, so their code is unchanged.
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
