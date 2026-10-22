# Memory and Safety Opcodes

VM memory management and the safety opcodes.

## VM Memory Management

These opcodes implement the C standard library heap (the default; `-V`/`--no-vm-heap` routes `malloc`/`free`/etc. to the host allocator instead).

| Opcode | Arguments | Description |
|--------|-----------|-------------|
| `MALC` | `REG_A0` = size | Allocate; pointer returned in `REG_A0` |
| `ALCA` | `REG_A0` = size | Allocate a bare `__builtin_alloca` call's storage (automatic, **frame**-scoped — lives until the function returns); pointer returned in `REG_A0`. Identical register/ABI shape and `AllocHeader`/`sorted_allocs` tracking to `MALC` (so `CHKB`/`CHKBN`/`CHKP3`/`DYNOBJSZ` see it exactly like any other allocation) but the resulting `AllocHeader.kind = ALLOC_KIND_ALLOCA` excludes it from leak-detection's `AllocRecord` list — it is never meant to be freed by the guest program. Reclaimed only at frame exit (`LEV3`, never `HREL`) — see the Heap Reclamation section below |
| `ALCV` | `REG_A0` = size | Allocate a VLA's backing storage (automatic, **block**-scoped — lives until the end of the declaring block, C11 6.8.3); pointer returned in `REG_A0`. Identical shape to `ALCA`, tagged `AllocHeader.kind = ALLOC_KIND_FRAME` instead. Split from `ALCA` specifically because the two have different lifetimes: reusing one `AllocKind` for both would let a block-exit reclaim (`HREL`) sweep a still-live bare-`alloca`'d block declared in the same block, corrupting it the moment the address was reused. `Node.is_vla_alloca_call` (set only by `new_alloca()`, `src/parse_init.c` — reached only through a VLA declaration's lowering, `ND_ASSIGN(ND_VLA_PTR, alloca(...))`) is what tells codegen which of `ALCA`/`ALCV` to emit for a given `alloca(...)` call |
| `ALCB` | `REG_A0` = size | Allocate a `__block` variable's heap box; pointer returned in `REG_A0`. Same shape as `ALCA`/`ALCV`, tagged `AllocHeader.kind = ALLOC_KIND_BLOCK_BOX` instead — split into its own opcode so heap reclamation targeting `ALLOC_KIND_FRAME`/`ALLOC_KIND_ALLOCA` storage can never sweep a `__block` box, which `Block_copy` is expected to let legitimately outlive its declaring frame. Before this split, `ALCA` alone backed both under a single `AllocHeader.is_internal` bool |
| `HMRK` | `depth` (i32 immediate) | Heap-reclamation watermark: push `{vm->bp, depth, vm->heap_ptr}` onto `vm->heap_marks`, truncating any stale entry for this `(bp, depth)` first (loop re-entry, backward `goto`). Emitted at the start of any block that declares a VLA, only when reclamation could apply (see the Heap Reclamation section below). Identical single-word-immediate shape to `SCOPEIN` |
| `HREL` | `depth` (i32 immediate) | The matching block-exit release for `HMRK`: find this `(bp, depth)`'s mark and rewind `vm->heap_ptr` to it, sweeping only `ALLOC_KIND_FRAME` (VLA) storage — never a bare alloca or a `__block` box. Emitted at the natural (fall-through) exit of the same block `HMRK` was emitted for; a `break`/`continue`/`goto`/`return` out of the block skips it, which only forfeits that block's reclamation (never over-reclaims) |
| `MFRE` | `REG_A0` = ptr | Free pointer (detects double-free) |
| `MCPY` | `REG_A0` = dest, `REG_A1` = src, `REG_A2` = count | `memcpy` |
| `MSET` | `REG_A0` = dest, `REG_A2` = count | `memset` to 0; backs `ND_MEMZERO` (pre-zero for partial aggregate initialisers) |
| `REALC` | `REG_A0` = ptr, `REG_A1` = new_size | `realloc` |
| `CALC` | `REG_A0` = nmemb, `REG_A1` = size | `calloc` |
| `REALCA` | `REG_A0` = ptr, `REG_A1` = nmemb, `REG_A2` = size | `reallocarray`; overflow-checked `nmemb * size` delegates to `REALC`'s logic, returning `NULL` (original allocation untouched) on overflow rather than truncating |
| `MALCA` | `REG_A0` = size, `REG_A1` = alignment | `aligned_alloc`; pointer returned in `REG_A0` (`NULL` if `alignment` isn't a power of two) |
| `PMEMA` | `REG_A0` = memptr, `REG_A1` = alignment, `REG_A2` = size | `posix_memalign`; status (`0`/`EINVAL`/`ENOMEM`) returned in `REG_A0`, allocated pointer written through `memptr` |

`MALC`/`CALC`/`REALC` allocate with the default 8-byte alignment; `MALCA`/`PMEMA`
 pad the bump pointer *before* the `AllocHeader` so the returned user
pointer lands on the requested alignment, while every consumer that recovers
the header via `((AllocHeader*)ptr) - 1` (`MFRE`, `REALC`, `DYNOBJSZ`, `CHKB`,
`CHKBN`, `CHKP3`, `CHKT3`) keeps working unmodified. Padding is only ever added
upward and is never reclaimed, consistent with the bump allocator having no
free-list reuse. Both opcodes share `MALC`'s canary/poisoning/leak-detection/
tagging tail via a common `vm_heap_bump_alloc_ex` helper, so `aligned_alloc` and
`posix_memalign` allocations get the same heap safety coverage as `malloc`.
`ALCA`/`ALCV`/`ALCB` share the same helper with `AllocHeader.kind` set to
`ALLOC_KIND_ALLOCA`/`ALLOC_KIND_FRAME`/`ALLOC_KIND_BLOCK_BOX` respectively —
the sole difference from `MALC` (`ALLOC_KIND_USER`).

### Heap Reclamation

The VM heap is a pure bump allocator — `heap_ptr` only ever advances, and
`MFRE` never moves it back (it only sets `AllocHeader.freed`). `HMRK`/`HREL`
(block exit) and an equivalent check in `LEV3` (frame exit) rewind it back
down for `alloca`/VLA storage specifically, when doing so is provably safe:

- **Gate.** Reclamation requires `vm->heap_reclaim_enabled`, computed once
  per top-level call (`cc_run_at_regs`, right after the `DYNOBJSZ`
  text-segment scan resolves `dynobjsz_present`) as
  `cc_heap_reclaim_flags_ok(vm->flags) && !vm->dynobjsz_present`.
  `cc_heap_reclaim_flags_ok` (`src/cccc.h`) requires every address-keyed
  safety feature to be off: bounds checks, UAF/dangling-pointer detection,
  memory tagging (`CCCC_POINTER_CHECKS`), type checks, uninitialized-read
  detection, leak detection, and heap canaries — a reclaimed address can be
  handed out again by a later allocation, and every one of those features
  relies on that never happening. True at `-0`; false at `-1`/`-2`/`-3`, or
  under any explicit combination of those flags. `DYNOBJSZ` has the
  identical hazard (it resolves interior pointers through `sorted_allocs`
  regardless of which flags are set) but isn't resolvable until after a
  full text-segment scan, which can't run until codegen has finished — so
  codegen.c uses `cc_heap_reclaim_flags_ok` alone (the flags-only, "is this
  even worth emitting" half) to decide whether to emit `HMRK`/`HREL` at
  all, while every opcode handler re-checks the complete
  `vm->heap_reclaim_enabled` at runtime and no-ops if it's false.
- **Threads.** `heap_marks` and `heap_owner` are per-thread (swapped with
  `ExecState` like `frame_epochs`; main is owner 0, each worker takes
  `++vm->next_heap_owner` in `link_thread`). Every allocation stamps
  `AllocHeader.owner`, and `heap_rewind_to` treats another thread's entry
  as pinning. Because a thread's mark can go stale (another thread rewinds
  below it and allocates across it), the rewind never goes below the end
  of the highest kept entry. A worker ending through `pthread_exit`/
  `thrd_exit` has its leftover marks truncated in `vm_thread_start`
  (`cccc_heap_marks_unwind_thread`). Contended threads reclaim only
  partially.
- **Mechanism.** `heap_rewind_to` (`src/ops.c`) scans `vm->sorted_allocs`
  backward from the top while each entry's base address is at or above the
  target watermark — this is exactly the contiguous suffix of allocations
  made since the mark was taken, since the bump allocator's append order
  is address order. It stops at the first entry whose `AllocKind` isn't
  sweepable for the caller (`HREL` sweeps only `ALLOC_KIND_FRAME`; `LEV3`
  and a guest `longjmp` sweep both `ALLOC_KIND_FRAME` and
  `ALLOC_KIND_ALLOCA`) — a genuine `malloc` (`ALLOC_KIND_USER`) or a
  `__block` box (`ALLOC_KIND_BLOCK_BOX`) always pins the bump pointer at
  its own end, giving a correct *partial* rewind rather than refusing the
  whole watermark. `sorted_allocs` is truncated to match, which is what
  keeps its "append order is address order" invariant true even though
  addresses can now be reused (see its own comment, `src/cccc.h`).
- **CALLT / longjmp.** A tail call (`CALLT`) reuses the caller's frame
  before the caller's own `LEV3` would run, and can carry a heap-resident
  VLA/alloca pointer as one of its own arguments — so `CALLT` only drops
  the caller's `heap_marks` entries (never rewinding) rather than
  reclaiming, leaving that storage to live on as it does today. A guest
  `longjmp` (which skips every abandoned frame's `LEV3`) and the host-level
  longjmp path used by a failed `__builtin_assert` (`testing.c`) both do
  reclaim, mirroring `frame_epoch_truncate_to`'s exact monotonic-address
  argument for discarding every entry belonging to a frame deeper than the
  target.

## Safety Opcodes

These are emitted by the compiler when the corresponding safety flag is set.  At runtime they are no-ops if the flag is cleared, so uninstrumented bytecode runs at full speed.

| Opcode | Description | Controlled by |
|--------|-------------|---------------|
| `CHKB` | Array bounds check on `base + scaled_offset` (`p + n`, and via subscript desugaring `*(a+i)`, `a[i]` too), resolving `base` (exact or interior pointer) via `vm->sorted_allocs`. **Formation-time** check only: a result exactly one past the allocation's end (`eff == size`) is allowed, since forming such a pointer is legal C — see `CHKD` below for the dereference-time check that catches an actual out-of-bounds access | `CCCC_BOUNDS_CHECKS` |
| `CHKBN` | Array bounds check on `base - scaled_offset` (`p - n`) — `CHKB`'s subtracting-form sibling; same operand shape, resolution, and one-past-the-end formation allowance, opposite sign. Never emitted for a pointer *difference* (`&a - &b`, result type `ptrdiff_t`/`long`) — `scaled_offset` there is `new_sub`'s ptr-ptr divide result, not a scaled byte offset, so bounds-checking it would compare an unrelated value against the allocation's size | `CCCC_BOUNDS_CHECKS` |
| `CHKI` | Uninitialised-variable read check (`bp+offset`) | `CCCC_UNINIT_DETECTION` |
| `MARKI` | Mark variable at `bp+offset` as initialised | `CCCC_UNINIT_DETECTION` |
| `CHKPA` | Validate pointer arithmetic against provenance | `CCCC_INVALID_ARITH` + `CCCC_PROVENANCE_TRACK` |
| `MARKP` | Record pointer provenance (`origin`, `base`, `size`) | `CCCC_PROVENANCE_TRACK` |
| `CHKP3` | Pointer validity (NULL, UAF, heap range, dangling stack deref), resolving the pointer (exact or interior) via `vm->sorted_allocs` | `CCCC_POINTER_CHECKS` |
| `CHKA3` | Pointer alignment check | `CCCC_ALIGNMENT_CHECKS` |
| `CHKT3` | Heap type-tag check on dereference (effective-type model), resolving the base pointer via `vm->sorted_allocs` | `CCCC_TYPE_CHECKS` |
| `CHKR` | Checked-pointer range check (`[[cccc::single/array/ntarray]]`): traps unless `addr != 0 && lo <= addr && addr + size <= hi` | `CCCC_CHECKED_BOUNDS` |
| `CHKRO` | Optional checked-pointer range check: identical to `CHKR`, except the sentinel `lo == (char*)-1 && hi == (char*)0` is a no-op instead of a violation | `CCCC_CHECKED_BOUNDS` |
| `CHKNT` | Checked-pointer null-terminator guard for `[[cccc::ntarray]]` (`count()`/`byte_count()`/`bounds()`): traps a store of a non-zero value into the widened terminator slot (`addr == hi - elem_size && val != 0`) | `CCCC_CHECKED_BOUNDS` |
| `CHKNTZ` | Checked-pointer null-terminator guard for the memcpy-lowered `ntarray` pointees `CHKNT` cannot reach (struct/union, wide `_BitInt`/`_Decimal`): traps unless every byte at the source address is zero, checked before the `MCPY` it guards | `CCCC_CHECKED_BOUNDS` |
| `CHKAB` | Checked-pointer assignment-time bounds implication: traps unless `slo <= val && val <= shi` | `CCCC_CHECKED_BOUNDS` |
| `CHKDC` | `dynamic_check(cond)` (`__builtin_cccc_dynamic_check`/`_Dynamic_check`): traps unless `regs[rs_val] != 0` | `CCCC_CHECKED_BOUNDS` |
| `CHKBS` | Static-size array bounds check: traps unless `0 <= regs[rs_off] <= limit` (immediate). The limit is the array's byte size, less the accessed bytes when the add is dereferenced at once. Covers the stack/global arrays `CHKB` cannot resolve. Format `[CHKBS][rs_off:8|unused:8][limit:i64]`; gated on `CCCC_BOUNDS_CHECKS`. |
| `HOLE` | Fills reclaimed text inside live code so instruction walkers stay aligned. Executing it is a runtime error. Format `[HOLE]`. |
| `CHKD` | **Dereference-time** bounds check, `CHKB`/`CHKBN`'s formation-time counterpart: traps unless `off + access_size <= header->size`, resolving the dereferenced address via `vm->sorted_allocs` the same way `CHKB`/`CHKP3` do. Emitted at every load/store site that reaches memory: scalar loads/stores, struct/union/wide-`_BitInt`/`_Decimal` `MCPY` copies, vector `VLDR`/`VSTR`, and the atomic ops `ALDR`/`ASTR`/`AXCHG`/`ACAS` (`ACAS` gets two — object pointer and `expected` pointer). Silently passes for a NULL pointer (`CHKP3`'s job) or a non-heap address (no bound known, same limitation `CHKB`/`CHKBN` already have) | `CCCC_BOUNDS_CHECKS` |

`CHKB`/`CHKBN` and `CHKP3` resolve their pointer's containing allocation via
the same `sorted_allocs_find` binary search `DYNOBJSZ` uses: the
largest tracked base address ≤ the pointer. This lets both checks recognise
**interior pointers** (`p = q + k`) into a heap allocation, not only exact
base pointers — an out-of-bounds index or a use-after-free reached through
an interior pointer is now caught. `CHKB`/`CHKBN`'s bound is
`AllocHeader.size` (the aligned/usable size); a negative effective offset is
only rejected once it steps before the *resolved allocation's* start, so
`p[-1]` on an interior pointer that stays within the allocation is valid.
Note the gaps this leaves: `AllocHeader` only exists for VM-heap
allocations, so **`CHKB`/`CHKBN` have no bound at all for a stack or global
base**, and reject nothing there. Subscripts and pointer-adds directly over a
fixed-size array are covered instead by `CHKBS`, whose limit is known
at compile time.

**Formation vs. dereference.** Forming a one-past-the-end pointer on
heap memory (`p + n` where `n == size`) is legal C — only dereferencing it
is undefined. `CHKB`/`CHKBN` allow it (`eff == size` is not an error) since
they run at pointer *formation*; `CHKD` is a separate check that runs at
every *dereference* site and traps on exactly this case, so `int *e = p + 4;`
(forming) is clean while `p[4] = 1;`/`*e = 1;` (dereferencing) still traps.
This split exists because `CHKB`/`CHKBN` used to be the *only* runtime check
on a subscript at all (`a[i]` desugars to `*(a+i)`), so simply relaxing
their comparison to `>` would have silently stopped a genuine out-of-bounds
access (`a[size]`) from being caught. The resolution relies on
`heap_alloc_for_ptr`'s existing inclusive upper bound (`off > h->size` is
its rejection condition, not `off >= h->size`), which is what lets `CHKD`
reliably resolve the allocation an exactly-one-past address belongs to.
`CHKD` has the same "no bound known" gap as `CHKB`/`CHKBN` for a stack or
global array (no `AllocHeader` to resolve against). It is also emitted
ahead of the atomic ops (`ALDR`/`ASTR`/`AXCHG`/`ACAS`) — this was
initially deferred because `AXCHG`/`ACAS`'s own operand words carry the
register-aliasing hazard, but a standalone `CHKD` instruction emitted
ahead of them never touches that word, so it doesn't reopen it. `CHKR`
closes the upper-bound
gap generally: its
`[lo, hi)` bounds come from the checked pointer's declaration
(`count()`/`byte_count()`/`bounds()`, or the implicit `[p, p+sizeof(T))` for
`[[cccc::single]]`), recomputed at each checked access, never from
`vm->sorted_allocs` — so it is uniform across heap, stack and global storage,
unlike every other opcode in this table. See
[safety.md](safety-checked-pointers.md#checked-pointers) for the full attribute/lowering
reference. Like every opcode in this table, `CHKR` has no equivalent in
`-c=native`/`-m`/`-c=generated` output — VM-only by design; those modes warn
and ignore `--checked-pointers` rather than emitting an inline check.
`CHKA3` is unaffected — alignment is pure address arithmetic and was already
correct for interior pointers.

`CHKRO` is `CHKR`'s sibling for a checked-pointer-bounds-propagation
candidate (`src/parse_checked.c`'s `propagate_checked_bounds()`) that is only
checked-rooted on *some* paths, not every path — "OPT" in
[safety.md](safety-checked-pointers.md#checked-pointers)'s propagation writeup, as opposed to
a "FULL" candidate (every store checked-rooted), which still emits plain
`CHKR` with identical codegen to before OPT candidates existed. An OPT candidate's snapshot
temps are refreshed at every assignment, rooted or not — a non-rooted store
writes the `[lo=-1, hi=0)` sentinel instead of skipping the refresh, and the
temps are also seeded with it at function entry — so whichever store
actually executed on the current path (or none at all) is what `CHKRO` sees.
This makes propagation exact per executed path with no CFG/join/fixpoint
needed: a static, join-based dataflow pass would still leave `q[i]` in `q =
malloc(...); if (c) q = p; q[i];` permanently unchecked (the fact isn't live
on every incoming edge of the join), whereas `CHKRO` enforces it exactly on
the runs where `c` took the `q = p` branch. Wire format is identical to
`CHKR` (`op_CHKR_fn`/`op_CHKRO_fn` in `src/ops.c` share one `chkr_common()`
body, differing only in whether the sentinel short-circuits).

`CHKNT` covers the one gap `CHKR`'s bounds widening for
`[[cccc::ntarray]]` opens: `count()`/`byte_count()`/`bounds()` all widen the
checked range by one element (`sizeof(T)` bytes at the declared end of the
range) so the terminator slot is a legal write target (`CHKR` already
enforces this), but nothing stopped that write from putting a non-null value
there and silently destroying the invariant the widening exists to serve.
`CHKNT` is emitted from the store path (`src/codegen_expr.c`'s `ND_ASSIGN` case),
not from `gen_addr` alongside `CHKR`, because it needs the value being
stored, which `gen_addr` never sees. A second emission site sits in
the `ND_CAS` case: `to_assign()` (`src/parse_expr.c`) desugars a read-modify-write
(`s[n] += 1`, `s[n]++`) into a synthesized store deref that now carries the
same checked-pointer fields as the original access, so it reaches the
`ND_ASSIGN` site like any other checked store; an `_Atomic`-qualified
`ntarray` element's RMW desugars into a CAS loop instead, so its `ND_CAS`
node carries the fields and gets its own `CHKNT` emission, checking the
CAS's *desired* value ahead of the `ACAS` opcode. It deliberately does **not** attempt to
verify a null terminator is present anywhere in the declared range: in
Checked C, `count(n)` on an `_Nt_array_ptr` is a *lower* bound on the valid
extent (`count(0)` is a legal, terminator-free declaration), so a presence
scan would false-positive on conforming code, and locating the real
terminator would require reading past `hi` — exactly the unbounded read this
feature exists to prevent. See [safety.md](safety-checked-pointers.md#checked-pointers)'s
"Terminator invariant" section for the full reasoning and its known gaps.

`CHKNT` also propagates across a `CHKR`/`CHKRO` bounds-propagation
candidate (`src/parse_checked.c`'s `propagate_checked_bounds()`): `Obj.checked_prop_
nt_elem` carries the terminator-slot fact (non-zero iff every checked-rooted
store into the candidate was `ntarray`-rooted at the same pointee element
size) alongside the already-propagated `[lo, hi)` snapshot, so a store
through a *propagated* pointer into an `ntarray` source's widened terminator
slot emits `CHKNT` exactly like a direct-access store does. The
read-modify-write and `_Atomic` desugars are covered too, via a
`Node.checked_rmw_mirror` back-link `to_assign()` sets on the original deref
at parse time (before propagation has resolved anything) so the propagation
pass's attach walk can find and stamp the synthesized RMW store node once
its bounds are known.

`CHKNTZ` covers the pointee types `CHKNT` structurally cannot: a
struct/union or wide `_BitInt`/`_Decimal` assignment never puts its value in
a single register at all — `ND_ASSIGN`'s codegen routes these through a
dedicated memcpy branch (`src/codegen_expr.c`, ahead of the generic scalar-store
path `CHKNT` is emitted from) that stages a source address and a
destination address for `MCPY`. `CHKNTZ` is emitted in that branch, right
before the `MCPY`, scanning `checked_access_size` bytes at the *source*
address for any non-zero byte — the aggregate analogue of `CHKNT`'s
value-register comparison. Running before the copy (rather than scanning
the destination afterward) means the terminator slot is never actually
clobbered by a write that turns out to violate the invariant, matching
every other `CHK*` opcode's before-the-effect timing. `float`/`double`
pointees stay on `CHKNT` itself: their value is transferred out of the
`FReg` file into an integer register first (`FR2R`/`FR2R_F32`, the same
bit-pattern-transfer idiom used elsewhere to move a flat-double value into
an integer register), then compared to 0 exactly like an integer store —
`FR2R_F32`'s payload is the raw `float` bits, so `-0.0f` (non-zero bits,
even though it compares equal to `0.0f`) correctly traps. `long double`
pointees are excluded from both opcodes: its widened terminator slot is
16 bytes but the actual store is an 8-byte flat-double `FSTR`, so no
existing opcode inspects the full stored representation.
`checked_nt_pointee_supported()` (`src/parse_checked.c`) is the single gate deciding
which pointee types set `checked_nt_terminator` at all — both the
direct-access site (`compute_checked_bounds()`) and the propagation
attach site (`checked_prop_attach_scan()`) call it, so `CHKNT`/`CHKNTZ`
selection and propagation agree on exactly the same supported-pointee set.

`CHKAB` is `CHKR`'s counterpart for the *opposite* trust direction.
Where propagation only ever widens trust
into a previously-unchecked target, `CHKAB` *verifies* trust when assigning
into a target that is itself already declared checked: `src/parse_checked.c`'s
`verify_checked_assign_bounds()` rewrites `q = E;` (both `q` and `E`
declared-checked, `E` a direct source, not a propagated one) into
`(temp = E's own bounds), (q = E)`, and codegen emits `CHKAB` twice after the
store — once against `q`'s own declared lower bound, once against its upper
bound — enforcing that `E`'s bounds imply `q`'s. `q`'s own bounds are
evaluated *after* the store (self-referencing on `q`'s own value, e.g.
`[q, q + m*sizeof(T))` for `count(m)`); the source's bounds are snapshotted
into temps *before* it, the inverse of `CHKR`'s own propagation-snapshot
ordering. `CHKAB` is reused, unmodified, by the `[[cccc::dynamic]]`
bounds cast (its desugar produces exactly this `q = E` shape against a
compiler-generated target), and again by the caller-side bounds-safe
interface check (`rewrite_checked_call_args()`, src/parse_checked.c),
which rewrites a qualifying call argument into `(__ca = arg, __ca)` against
a hidden declared-checked local carrying the callee's own parameter bounds
(substituted with that call's actual arguments) — the resulting `__ca =
arg` assignment is then picked up by the *same*, unmodified
`verify_checked_assign_bounds()` walk described above, needing no third
code path of its own. See [safety.md](safety-checked-pointers.md#checked-pointers) for the
full writeup of all three.

`CHKDC` is `dynamic_check(cond)`'s runtime half: a single register
read, no address or allocation lookup at all, so it needs no
`sorted_allocs`/provenance resolution the way every other `CHK*` opcode in
this table does. Built only when `CCCC_CHECKED_BOUNDS` was already set at
parse time (`__builtin_cccc_dynamic_check`/`_Dynamic_check` parse to a
plain `ND_NULL_EXPR` otherwise, the same convention `__builtin_assume`
uses), so it never reaches `-c=native`/`-m`/`-c=generated` serialization in
the default build.

`CHKT3` is live (extended to byte granularity). It is emitted
by `emit_load_ex`/`emit_store_ex` (`src/codegen_emit.c`) right after `CHKP3`,
carrying the pointee's static `TypeKind` and size, and a 3-way mode:
`[rs:8|mode:8]` in the operand word, `[(size<<8)|expected_type:64]` as the
trailing immediate. `mode` is `CHKT3Mode` (`src/cccc.h`):

| Mode | Emitted by | Effect |
|---|---|---|
| `CHKT3_MODE_CHECK` (0) | a load | compares the shadow across `[ptr, ptr+size)` against `expected_type` |
| `CHKT3_MODE_STAMP` (1) | a store | establishes `expected_type` as the effective type for `[ptr, ptr+size)` |
| `CHKT3_MODE_CLEAR` (2) | a union member store | erases (rather than stamps) `[ptr, ptr+size)`, so a later access through a different union member doesn't false-positive |

Type tracking is a **byte-granular shadow** (`vm->type_shadow_pages`,
logically one byte per heap byte, indexed by `(char*)ptr - vm->heap_seg`,
physically a sparse page table of 64 KiB pages — see below), replacing the
earlier one-`type_kind`-per-allocation model — this is what lets a struct
member or array element be checked independently of the allocation's base
pointer or any other member/element. `TY_VOID` (0) means "no effective type
established", mirroring C11 §6.5p6: fresh heap bytes start there; a store
stamps the range it writes; a load checks the range it reads. A range whose
bytes carry more than one stamped type (e.g. straddling two prior stamps) is
treated as no-info rather than guessed at. `char`-typed accesses are always
legal (§6.5p7): a `char` load never checks, and a `char` store clears rather
than stamps. `CHKT3` resolves the containing allocation via the same
`heap_alloc_for_ptr`/`sorted_allocs_find` helper as `CHKB` and `CHKP3`
purely to find the allocation's liveness (`freed`) and
range — the type information itself lives in the shadow, not in
`AllocHeader`.

There are two shadow instances (`TypeShadowSeg`, `src/cccc.h`), one per
tracked segment: `vm->heap_shadow` (the original scope) and
`vm->data_shadow`, covering globals (`static`/file-scope variables).
`type_shadow_locate` (`src/ops.c`) resolves which segment (if either) a
given `[ptr, ptr+len)` range falls wholly inside; `op_CHKT3_fn` itself
still gates on `heap_alloc_for_ptr`/`sorted_allocs_find` exactly as before
for any address inside the heap segment — a heap-range address that isn't
inside a *live* allocation reports nothing at all here (`CHKP3`'s job) —
and skips that resolution entirely for an address outside the heap
segment, deferring to `type_shadow_locate` to decide whether it's a global
or an untracked address (e.g. the stack). Each segment's page *vector* is
lazily grown (`type_shadow_ensure`, `src/ops.c`) to cover its committed
size whenever `CCCC_TYPE_CHECKS` is set, so it stays in sync even if the
flag is toggled on mid-run by `#pragma cccc config(safety=N)`; individual
64 KiB pages are allocated only when a stamp first touches their range, and
freed back to `NULL` the moment a clear zeroes one in full
(`type_shadow_fill`, `src/ops.c`), so host memory for the shadow tracks the
*live* stamped footprint rather than either segment's total reservation —
a large allocate-then-free pattern on the heap reclaims its shadow pages
once the allocation is freed. A missing page reads back as all `TY_VOID`
("no info"), identical to an allocated all-zero page.

Every heap-mutating opcode keeps the heap shadow current: `MFRE` clears on
free; `MCPY` (backing struct/union assignment and the restrict
memcpy-loop lowering) propagates source shadow onto destination, mirroring
`memcpy` copying the effective type along with the bytes; `REALC` (always a
fresh bump allocation, never grown in place) carries the old block's shadow
to the new address before freeing the old one. (`MALC`/`CALC`/`MALCA` need
no explicit clear for a fresh allocation: with the page-chunked shadow, a
range no stamp has ever touched already reads back as `TY_VOID` at zero
host cost.) Guest `memcpy`/`memmove` route through shadow-aware host shims
(`cccc_shim_memcpy`/`cccc_shim_memmove`, `src/stdlib/string.c`) that call
`cc_type_shadow_copy` after the real libc call runs — this also covers a
copy between the heap and a global, since `type_shadow_copy` resolves src
and dst independently. Every *other* host function reachable through
`CALLF` might write heap or global bytes with no VM-level hook at all
(`fread`, `read`, `scanf`, any other FFI call) — `op_CALLF_fn` conservatively
clears shadow state reachable through an integer argument register before
such a call runs, so a write the VM can't observe can never leave a stale
stamp that later false-positives. The clear resolves either tracked segment
(`ffi_shadow_clear_extent`, `src/ops.c`): a heap address clears by its
allocation's extent (base pointer to `header->size`, via
`heap_alloc_for_ptr`, unaffected by which byte of the allocation the
pointer argument actually points at); a data-segment (global) address has
no allocation header to bound it, so it clears from the pointer to the end
of the emitted data segment (`vm->data_ptr`). `RETBUF` clears its handed-out
`return_buffer_pool` slot's shadow range before returning it: those slots
live in `data_seg` and rotate between every struct-returning call, so
without the clear a slot would carry a stale stamp from whichever struct
type was returned through it last.

`op_CALLF_fn`'s clear is classified per host function by name (
`ffi_shadow_classify`/`ffi_shadow_rules`, `src/ops.c`), recovering coverage
the blanket clear used to destroy for functions whose write behavior is
statically known:

| `FfiShadowClass` | Effect | Examples |
|---|---|---|
| `FFI_SHADOW_HANDLED` | no clear — the shim already propagated | `memcpy`, `memmove`, `qsort` |
| `FFI_SHADOW_READONLY` | no clear at all — never writes through any pointer arg | `strlen`, `strcmp`, `memcmp`, `fwrite`, `bsearch`, ... |
| `FFI_SHADOW_BOUNDED` | clear narrowed to `[args[out_arg], args[out_arg]+len)`, where `len` comes from another argument (or a fixed size, for `strtol`/`strtod`'s `*endptr`), clamped against the remaining bytes in whichever segment resolves the pointer | `fread`, `snprintf`, `read`, `recv`, `strncpy`, ... |
| `FFI_SHADOW_DEFAULT` (unclassified) | whole-object clear (heap allocation or, for a global, pointer-to-end-of-data-segment) | everything else, including `printf`/`scanf` (`%n`) |

A `BOUNDED` entry only narrows the clear for its one designated argument;
every *other* pointer-shaped argument to that same call (e.g. `snprintf`'s
variadic `%s` arguments) still gets the default whole-object clear.
Since an unclassified name behaves exactly as before, adding or widening a
rule can only ever reduce clearing relative to today's behavior, never
introduce a false positive.

`qsort` is `HANDLED` by `wrap_qsort` (`src/stdlib/stdlib.c`) rather
than by a table entry, since narrowing its clear needs runtime information
(the actual pre/post shadow contents) a static rule can't express: `qsort`
only reorders whole `size`-byte elements, never rewrites their bytes, so if
every element's shadow pattern already matches element 0's before the host
`qsort()` call, that pattern is invariant under any permutation and the
shadow needs no clear at all — checked via
`cc_type_shadow_elements_uniform` (`src/ops.c`), unconditionally both
before the call and again immediately after. The post-check runs even when
the pre-check found a uniform range (e.g. an already-cleared, all-`TY_VOID`
range, itself trivially uniform): the comparator is guest code, reentered
through the callback trampoline, and its own loads/stores run through
ordinary CHKT3 checks, so a comparator that writes through its arguments
mid-sort stamps the shadow at that element's pre-move position, which the
post-check must still catch before `qsort` relocates the bytes elsewhere.
Either check finding a non-uniform range clears exactly
`[base, base+nmemb*size)` via `cc_type_shadow_clear_range` -- the range a
host `qsort()` call can touch, not the whole allocation. `bsearch`'s host
half writes through no argument at all, so it needs no runtime check and is
a plain `READONLY` table entry; its comparator is shadow-tracked the same
way `qsort`'s is.

Reusing a heap buffer as a different type — legal C — never false-positives,
since the next store simply re-stamps the effective type for the bytes it
touches. Union member access is exempted in both directions via
`vm->compiler.in_union_member_access` (set/cleared around `ND_MEMBER`
codegen when the immediate parent expression's type is `TY_UNION`): a union
load skips CHKT3 emission entirely; a union store emits `CHKT3_MODE_CLEAR`
instead of `CHKT3_MODE_STAMP`. Covers the heap and globals; stack
subobjects remain untracked (deferred — stack-slot reuse across frames
would need its own liveness bookkeeping, mirroring `frame_epochs`/
`stack_intervals`, to avoid reopening the false-positive class those exist
to prevent in the dangling-pointer detector).
