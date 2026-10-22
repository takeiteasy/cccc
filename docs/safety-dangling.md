# Dangling Pointer Detection

Catch dereferences of pointers into dead stack frames.

## `--dangling-pointers` — Dangling stack pointer detection

- Enforced in `CHKP3`, the same gate that runs before every pointer
  dereference under `--pointer-checks` / `-3`. Three layers, all
  dereference-time (no creation-time escape analysis decides *correctness* —
  escape analysis only prunes a redundant recording, see below):
  1. **Range check.** The VM stack grows downward, so every *live*
     local sits at an address `>= vm->sp`, and any address inside the stack
     reservation but below the current `vm->sp` belongs to a frame that has
     already returned. A single unsigned range check
     (`stack_seg <= ptr < sp`) is precise by construction — cheap, no
     hashmap needed for this layer.
  2. **Frame-epoch liveness.** Every activation gets a monotonic
     epoch at `ENT3`; every `&local` (`LEA3`) tags the resulting address
     with the current frame's epoch in a side table (`stack_ptr_epochs`),
     the direct stack analogue of how heap UAF detection tags an
     allocation's creation generation in `ptr_tags`. A pointer to a local
     is valid iff the frame that created it is still live, so `CHKP3` flags
     a dereference whose tagged epoch is no longer in the live set
     (`live_epochs`) — regardless of whether the address happens to sit
     above or below the *current* `sp`. This closes the range check's gap:
     a pointer passed *deeper* into another call reuses the dead frame's
     memory (`ptr >= sp` again there), which layer 1 alone would miss, but
     its tagged epoch is still absent from `live_epochs` and layer 2 still
     catches it. Since, a dead exact tag no longer concludes dangling
     on its own: stack addresses are reused across frames, so a returned
     sibling's own exact-recorded escaping local can leave a stale tag at
     an address a *live* frame has since legitimately reclaimed (e.g. a
     stack-spilled variadic vector argument's `va_arg` slot landing on a
     dead sibling's old `va_list` base). `CHKP3` now falls through to
     layer 3's `stack_interval_stab` on a dead exact hit and only reports
     dangling if no *live* interval covers the address either — the same
     prefer-live arbitration layer 3 already had for, now shared by
     layer 2
  3. **Interior interval-stabbing.** An interior stack pointer with a
     runtime-computed offset (e.g. `&arr[i]` for non-constant `i`) compiles
     to a base `LEA3` for `arr` plus a separate runtime `ADD`, so the
     *final* dereferenced address is never itself the one recorded in
     `stack_ptr_epochs` by layer 2 — only `arr`'s base is. `STKTAG`, emitted
     immediately after the base `LEA3` of any *escaping* array/struct local
     (and, unconditionally, of any `vector_size` vector local regardless of
     whether escape analysis proved it escaping: element access
     (`v[i]`) always re-derives the vector's address like a struct member,
     even for a vector that never leaves its frame), retains
     `[base, base+size)` tagged with the creating frame's epoch in
     `vm->stack_intervals`. Unlike `sorted_allocs` (the heap analogue) — a bump allocator whose base addresses are globally ordered and
     never reused — stack addresses *are* reused across frames, so
     intervals are retained rather than pruned on frame death; a dead
     frame's extent and a later live frame's extent can share the same
     addresses. Consulted only when layer 2's exact lookup misses, `CHKP3`
     resolves an interior address by **preferring the max-epoch *live*
     containing interval** — only when every containing interval is
     dead does it fall back to the plain max-epoch (dead) interval and
     flag it. Recency-by-epoch-order alone (epoch order *is* recency order,
     since activations get strictly increasing epochs) is not sound here:
     a live frame's own STKTAG range can coexist with an *unrelated* dead
     sibling frame's STKTAG range at the very same addresses (stack reuse
     across siblings, not just across a dead frame and its live successor),
     and the dead sibling can have a numerically higher epoch just by
     having run more recently before returning. Preferring any live
     containing interval resolves this regardless of epoch ordering,
     while a genuinely dangling interior pointer (no live interval covers
     it) is still flagged correctly.
- **What it catches:** dereferencing a pointer to a local whose function has
  returned, whether the dereference happens in the frame that got control
  back (or any ancestor of it — layer 1) or one or more calls *deeper* than
  that (layer 2), through a base pointer or an interior one with a
  runtime-computed offset whose owning array/struct escapes (layer 3) —
  e.g. `int *p = get_local(); return *p;`, `int *p = get_local(); use(p);`
  where `use` derefs `p`, and `int *p = &arr[i]` (runtime `i`) returned from
  `get_local` and derefed in a deeper call. This also covers deref-through-
  pointer cases that `CHKL` (see `--stack-instrumentation` below) does not
- **What it does not catch:** this is frame-granular detection, not
  block-granular — a `&local` whose *block* (not function) has exited
  while the frame is still alive is `CHKL`'s job, not this check's (see
  `--stack-instrumentation` below)
- **Local aggregate member/element access is not gated on `CHKP3` at all.** A local struct/union's own member access (`t.a`) lowers
  through `gen_addr()` + `emit_load`/`emit_store` exactly like a genuine
  pointer dereference, so `CHKP3` used to run unconditionally there too —
  and could find a *stale* exact tag left in `stack_ptr_epochs` by an
  unrelated, already-returned sibling frame's own escaping local that
  happened to reuse the same physical stack address, misreporting a
  plainly-live access to the current frame's own memory as dangling.
  `addr_is_local_frame` (`src/codegen_addr.c`) recognizes when `gen_addr`'s
  result is guaranteed to be a bp-relative address of the *current*
  function's own live frame — built entirely through the plain
  local-offset path, with no intervening pointer *value* load (captured
  variable, static-link chain, by-pointer aggregate param, `__block` heap
  wrapper, or an actual pointer dereference) — and skips `CHKP3` for that
  access entirely; a genuine pointer dereference (`p->a`, `p[i]` through a
  variable holding a passed/returned address) always bottoms out at a
  dereference in this classifier and stays fully checked. Deliberately not
  gated on whether the local's address escapes: accessing your own local
  from within your own still-running frame is safe regardless.
  **Related layer-2 exact-tag collision, resolved separately.**
  Array/vector *indexing* (`arr[i]`) and a stack-spilled variadic
  argument slot read by `va_arg` don't qualify for the `addr_is_local_frame`
  skip above (the address isn't always provably the current frame's own
  memory — e.g. the `va_arg` slot lives in the argument-passing area, not
  a declared local), so they can still hit a dead sibling frame's stale
  exact tag in `stack_ptr_epochs`. Rather than skip `CHKP3` at these sites fixed the collision at its source: layer 2 now falls through to
  layer 3's prefer-live `stack_interval_stab` on a dead exact hit instead
  of concluding dangling outright (see layer 2's own description above).
  A live frame's own STKTAG'd extent — e.g. a non-escaping vector or array
  local, or a stack-spilled `va_arg` read — now wins over an overlapping
  dead sibling's stale exact tag; a genuinely dangling exact-tag deref
  (nothing live covers the address) is still flagged. Verified with
  `tests/test_dangling_variadic_stack_spilled.c` (the stack-spilled
  `after8`/`wide` shapes from the ticket) plus manual probes of local
  array-indexing collisions
- **Own-array access:** a frame's access to its own local array (`buf[i]`,
  `m[i][j]`, `s.arr[i]`, `arr[i].f`, including `+=` and `++`) is never
  reported as dangling, even when a returned frame's tag sits inside the
  array. A stale tag at the array's *base* is cleared when the live frame
  takes that address.
- **Pointer access:** an array whose address is taken (`char *q = buf`,
  `&buf[i]`, passed to a callee, held in a struct) has its extent tagged live,
  so access through the pointer is not reported as dangling either.
- **Second consumer:** the epoch/interval bookkeeping above
  (`frame_epochs`, `live_epochs`, `stack_intervals`) is not exclusive to
  `--dangling-pointers` — `__builtin_dynamic_object_size` also stabs
  `stack_intervals` (via `DYNOBJSZ`) to size an escaping fixed-size stack
  array/struct/union reached through an opaque pointer, using the exact
  same max-epoch resolution and live-epoch trust check as layer 3 above.
  Using that builtin anywhere in the program activates this bookkeeping on
  its own, independently of `-1`/`-2`/`-3` — see [stdlib.md](stdlib.md)'s
  `__builtin_dynamic_object_size` entry.
- **Lazy per-function push:** activation is scoped to the functions
  that actually need it, not every call in the program. `ENT3` pushes a
  frame epoch only when that function's own body emits `STKTAG` (an
  escaping aggregate local/param) or a recorded `LEA3` (an escaping
  scalar, under `--dangling-pointers`) — the only two consumers of the
  *current top* epoch. A function with no escaping local/param of its own
  (the common case — plain wrapper/leaf functions) pushes nothing and is
  simply absent from `frame_epochs` for its entire activation; `LEV3`/
  `CALLT` retire an epoch only when the top entry's saved `bp` matches the
  frame currently unwinding, so this is self-synchronizing and needs no
  per-frame flag on the teardown side. This means a program that uses
  `__builtin_dynamic_object_size` sparingly (e.g. one `FORTIFY`-style
  wrapper) in an otherwise call-heavy program pays the epoch push/pop cost
  only in the functions on the path to that wrapper's escaping buffer, not
  on every call in the program — the same shape of win as pruning
  `LEA3` recording, but for the `ENT3`/`LEV3` push/pop itself (lazy
  per-function activation).
- Recording is pruned to addresses that provably *escape* their creating
  frame — a post-parse pass (`mark_addr_escapes` in `src/parse_analysis.c`)
  marks a local's `Obj.addr_escapes` when its address (or an array/struct
  it owns, interior-aware through `&arr[i]`/`&s.field`, or pointer
  arithmetic on that base such as `arr + i`) is observed as a call
  argument, a `return` operand, or stored into a pointer/aggregate
  lvalue (`++`, `--` and compound assignment on a function's own plain local,
  a member of one, or an element of a local array, are not escapes); `LEA3` carries a `LEA3_NO_RECORD` flag (set at codegen time) that
  tells `op_LEA3_fn` to skip the `stack_ptr_epochs` write whenever the
  creating var's address was never marked. This is the *opposite* polarity
  from the earlier escape-analysis mistake below: an **unmarked** local
  still records unconditionally (safe default), and only a *proven*
  non-escaping address is pruned — so a pattern the pass fails to
  recognize just costs a wasted hashmap entry, never a missed catch.
  Measured ~16% overhead on a call-heavy `-3` microbenchmark before this
  change; pruning is skipped (recording stays on) for any address that
  reaches a call/return/pointer-store, so that worst case is unaffected —
  the win is on the common non-escaping case (loop counters, in-place
  accumulators, and all compiler-internal `LEA3`s: static links, block
  descriptors, closure captures, and other slot addresses that only ever
  feed one immediate load/store)
- This replaces an earlier scope-exit check, which
  conflated "address taken" with "escaped" — it tracked every `&local` via
  the (now removed) `MARKA` opcode and aborted whenever any of them was
  still present at *function* exit, which is every non-consumed `&local`,
  not just ones that actually escaped. It had zero true-positive coverage
  and aborted on fully benign code (e.g. `int *p = &n; *p = 5;`)

### Example

`--dangling-pointers` (also enabled by `-3` / `--safety=max`) enforces two
dereference-time checks in `CHKP3`: a range check and a per-frame
epoch liveness check.

```c
// test_dangling_pointer.c - Dangling stack pointer example
int *get_local_address() {
    int x = 42;
    return &x;  // Address of local variable -- fine to take, fine to return.
}

int main() {
    int *ptr = get_local_address();
    int value = *ptr;  // Dereference of a dangling pointer -- caught here.
    return value;
}
```

```bash
$ ./cccc --dangling-pointers test_dangling_pointer.c

========== DANGLING STACK POINTER ==========
Dereferenced a pointer into a stack frame that has already returned
Address:    0x15fffffc8
Current SP: 0x15fffffe0 (address is below live stack -> frame is dead)
Current PC: 0x14 (offset: 20)
============================================
```

The stack grows downward, so any address that falls inside the stack
reservation but below the current stack pointer belongs to a frame that has
already returned — that's the range check firing above, the cheap
common case.

**The deeper-call case:** passing the dangling pointer one call
*deeper* and dereferencing it there defeats the range check alone, because
the deeper call's own frame reuses the same stack memory (the address is
`>= sp` again by the time it's dereferenced):

```c
// test_dangling_deref_deeper_call.c - Dangling deref through a deeper call
int *get_local(void) {
    int x = 42;
    return &x;
}

void use(int *p) {
    int y = *p;  // Dereference happens one frame deeper than get_local()'s
    (void)y;     // caller -- caught by the epoch check, not the range check.
}

int main(void) {
    int *p = get_local();
    use(p);
    return 0;
}
```

```bash
$ ./cccc --dangling-pointers test_dangling_deref_deeper_call.c

========== DANGLING STACK POINTER ==========
Dereferenced a pointer into a stack frame that has already returned
Address:    0x31fffffc8
Creating frame's epoch is no longer live (dereferenced through a deeper call)
Current PC: 0x2d (offset: 45)
============================================
```

Every `&local` is tagged at the moment it's taken with the current frame's
liveness epoch; `get_local()`'s epoch dies at `LEV3` along with the rest of
its frame, so `use()`'s dereference is flagged regardless of what address
range `use()`'s own frame happens to occupy.

**The interior-pointer case:** an interior stack pointer with a
runtime-computed offset (e.g. `&arr[i]` for non-constant `i`) doesn't go
through a single `LEA3` the way a plain `&local` or constant-offset
`&s.field` does — the base `arr` gets its own `LEA3`, but the final address
is computed by a separate runtime add, so it was never itself the address
tagged in `stack_ptr_epochs`. A deeper-call dereference through one of these
used to rely on the range check alone (and could be missed the same way
originally documented for plain locals) until `STKTAG` closed the gap:

```c
// test_dangling_interior_deeper_call.c - Dangling deref through an interior pointer
int *get_local(int i) {
    int arr[8];
    arr[i] = 42;
    return &arr[i]; // runtime offset -- not a single LEA3
}

void use(int *p) {
    int pad[8]; // reoccupies the same memory arr used, defeating the range check
    pad[0] = 0;
    int y = *p; // NOT caught by the range check alone; caught by.
    (void)y;
    (void)pad;
}

int main(void) {
    int *p = get_local(3);
    use(p);
    return 0;
}
```

```bash
$ ./cccc --dangling-pointers test_dangling_interior_deeper_call.c

========== DANGLING STACK POINTER ==========
Dereferenced a pointer into a stack frame that has already returned
Address:    0x167ffffb4
Creating frame's epoch is no longer live (dereferenced through an interior pointer into a deeper call)
Current PC: 0x4d (offset: 77)
============================================
```

`STKTAG`, emitted right after `arr`'s base `LEA3` because `&arr[i]` escapes
via the `return`, retains `arr`'s `[base, base+size)` extent tagged with
`get_local()`'s epoch. `CHKP3`'s exact `stack_ptr_epochs` lookup misses (the
dereferenced address is interior, not the tagged base), so it falls through
to the interval-stabbing lookup, finds `arr`'s retained interval containing
the address, and flags it — same as the plain-local case, one layer deeper.

**Discarded-value dereferences are still checked:** a dereference
whose result is unused (`*p;`, `(void)*p`) is checked exactly like one whose
result is kept — the result is discarded, not the access:

```c
int *p = get_local();
*p;  // Still caught, even though nothing reads the loaded value.
```
