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

`verify_checked_assign_bounds()` is a sibling pass over declared-checked targets, disjoint from propagation. It rewrites `q = E` into `(temp = source bounds), (q = E)` and emits `CHKAB rs_val, rs_slo, rs_shi` twice (target `lo`, then `hi`), which traps unless `slo <= val <= shi`. The source is snapshotted *before* the store because the store may overwrite it. The target's bounds are evaluated *after* the store because they refer to the target itself (`[q, q + m*sizeof(T))`). A propagated source is skipped since it may hold the sentinel and `CHKAB` has no sentinel-aware form.

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
