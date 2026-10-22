# Memory and Safety Opcodes

VM memory management and the safety opcodes. How the checks work is in [safety-impl.md](safety-impl.md).

## Memory management

These implement the C heap (the default; `-V`/`--no-vm-heap` sends `malloc` and friends to the host allocator).

| Opcode | Arguments | Description |
|---|---|---|
| `MALC` | `REG_A0` = size | Allocate; pointer in `REG_A0` |
| `ALCA` | `REG_A0` = size | Storage for a bare `__builtin_alloca`: frame-scoped, kind `ALLOC_KIND_ALLOCA`, reclaimed only at frame exit |
| `ALCV` | `REG_A0` = size | A VLA's storage: block-scoped (C11 6.8.3), kind `ALLOC_KIND_FRAME`. Separate from `ALCA` because a block-exit reclaim must not sweep a live `alloca` from the same block. `Node.is_vla_alloca_call` (set by `new_alloca()` in `src/parse_init.c`) tells codegen which to emit |
| `ALCB` | `REG_A0` = size | A `__block` variable's heap box, kind `ALLOC_KIND_BLOCK_BOX`. Separate so reclamation can never sweep a box that `Block_copy` lets outlive its frame |
| `HMRK` | `depth` (i32) | Push a reclamation watermark `{bp, depth, heap_ptr}` at the start of a block that declares a VLA, replacing any stale entry for the same `(bp, depth)` |
| `HREL` | `depth` (i32) | Block-exit release: rewind `heap_ptr` to the mark, sweeping only `ALLOC_KIND_FRAME`. A `break`, `continue`, `goto` or `return` out of the block skips it, which only forfeits reclamation |
| `MFRE` | `REG_A0` = ptr | Free; detects double free |
| `MCPY` | dest, src, count in `REG_A0`..`REG_A2` | `memcpy` |
| `MSET` | `REG_A0` = dest, `REG_A2` = count | Zero-fill; backs `ND_MEMZERO` (pre-zero for partial initializers) |
| `REALC` | ptr, new_size | `realloc` |
| `CALC` | nmemb, size | `calloc` |
| `REALCA` | ptr, nmemb, size | `reallocarray`; on overflow returns `NULL` and leaves the original untouched |
| `MALCA` | size, alignment | `aligned_alloc`; `NULL` if the alignment is not a power of two |
| `PMEMA` | memptr, alignment, size | `posix_memalign`; status (`0`/`EINVAL`/`ENOMEM`) in `REG_A0` |

`MALC`, `ALCA`, `ALCV` and `ALCB` share one bump-allocation helper (`vm_heap_bump_alloc_ex`) and differ only in `AllocHeader.kind`. `MALCA` and `PMEMA` pad the bump pointer *before* the `AllocHeader`, so the user pointer lands aligned while everything that recovers the header with `((AllocHeader*)ptr) - 1` keeps working. Padding is never reclaimed, and they share `MALC`'s canary, poisoning, leak and tagging tail.

### Heap reclamation

The heap is a pure bump allocator: `heap_ptr` only advances and `MFRE` just sets `AllocHeader.freed`. `HMRK`/`HREL` (block exit) and a check in `LEV3` (frame exit) rewind it for `alloca` and VLA storage when that is provably safe.

| Topic | Rule |
|---|---|
| Gate | `vm->heap_reclaim_enabled`, computed once per top-level call as `cc_heap_reclaim_flags_ok(vm->flags) && !vm->dynobjsz_present`. It needs every address-keyed safety feature off: bounds checks, UAF and dangling-pointer detection, memory tagging, type checks, uninitialized-read detection, leak detection, heap canaries. True at `-0`, false at `-1` and above |
| `DYNOBJSZ` | Has the same hazard but can only be found by scanning the text segment after codegen. Codegen uses the flags-only half to decide whether to emit `HMRK`/`HREL`; every handler rechecks the full condition at run time and no-ops if false |
| Threads | `heap_marks` and `heap_owner` are per thread (swapped with `ExecState`; main is owner 0). Every allocation stamps `AllocHeader.owner`, `heap_rewind_to` treats another thread's entry as pinning, and a rewind never goes below the end of the highest kept entry. `pthread_exit` and `thrd_exit` truncate a worker's leftover marks. Contended threads reclaim only partially |
| Mechanism | `heap_rewind_to` (`src/ops.c`) scans `sorted_allocs` backward while entries sit at or above the watermark, which is exactly the suffix allocated since the mark. It stops at the first entry not sweepable by the caller (`HREL`: `ALLOC_KIND_FRAME`; `LEV3` and `longjmp`: `FRAME` and `ALLOCA`), so a `malloc` or `__block` box pins the pointer at its end for a correct partial rewind. `sorted_allocs` is truncated to match |
| `CALLT` | A tail call reuses the caller's frame and may carry a heap-resident VLA pointer as an argument, so it only drops the caller's `heap_marks` entries and never rewinds |
| `longjmp`, failed `__builtin_assert` | Both reclaim, discarding every entry for frames deeper than the target |

## Safety opcodes

Emitted when the matching flag is set; they no-op at run time if it is cleared. None has a `-c=native`/`-m`/`-c=generated` equivalent except where [noted](safety-checked-interfaces.md#native-output).

| Opcode | Description | Flag |
|---|---|---|
| `CHKB` | Bounds check on `base + scaled_offset` (`p + n`, and `a[i]` via `*(a+i)`), resolving `base` (exact or interior) through `sorted_allocs`. Formation time: one past the end is allowed | `CCCC_BOUNDS_CHECKS` |
| `CHKBN` | `CHKB`'s sibling for `base - scaled_offset`. Never emitted for a pointer difference, whose operand is a ptr-ptr divide, not a scaled offset | `CCCC_BOUNDS_CHECKS` |
| `CHKD` | Dereference-time check: traps unless `off + access_size <= header->size`. At every load and store that reaches memory (scalar, struct/union/wide `_BitInt`/`_Decimal` `MCPY`, vector `VLDR`/`VSTR`, and `ALDR`/`ASTR`/`AXCHG`/`ACAS`, with two for `ACAS`). Passes silently for NULL (that is `CHKP3`'s job) and for non-heap addresses | `CCCC_BOUNDS_CHECKS` |
| `CHKBS` | Static-size array check: traps unless `0 <= regs[rs_off] <= limit`. The limit is the array's byte size, less the accessed bytes when dereferenced at once. Covers stack and global arrays `CHKB` cannot resolve. Format `[CHKBS][rs_off:8\|unused:8][limit:i64]` | `CCCC_BOUNDS_CHECKS` |
| `CHKI` / `MARKI` | Read check / mark initialized, at `bp+offset` | `CCCC_UNINIT_DETECTION` |
| `MARKP` / `CHKPA` | Record pointer provenance (`origin`, `base`, `size`) / validate arithmetic against it | `CCCC_PROVENANCE_TRACK` (+ `CCCC_INVALID_ARITH` for `CHKPA`) |
| `CHKP3` | Pointer validity: NULL, use after free, heap range, dangling stack dereference | `CCCC_POINTER_CHECKS` |
| `CHKA3` | Alignment (pure address arithmetic, already correct for interior pointers) | `CCCC_ALIGNMENT_CHECKS` |
| `CHKT3` | Effective-type check on dereference ([modes](#chkt3)) | `CCCC_TYPE_CHECKS` |
| `CHKR` | Checked-pointer range: traps unless `addr != 0 && lo <= addr && addr + size <= hi` | `CCCC_CHECKED_BOUNDS` |
| `CHKRO` | `CHKR`, but the sentinel `lo == -1, hi == 0` is a no-op (for pointers checked-rooted on only some paths) | `CCCC_CHECKED_BOUNDS` |
| `CHKNT` | Terminator guard: traps a non-zero store into the widened `ntarray` terminator slot (`addr == hi - elem_size && val != 0`) | `CCCC_CHECKED_BOUNDS` |
| `CHKNTZ` | The same for pointees stored through `MCPY`: traps unless every source byte is zero, checked before the copy | `CCCC_CHECKED_BOUNDS` |
| `CHKAB` | Assignment bounds implication: traps unless `slo <= val && val <= shi` | `CCCC_CHECKED_BOUNDS` |
| `CHKDC` | `dynamic_check(cond)`: traps unless `regs[rs_val] != 0`. No address lookup | `CCCC_CHECKED_BOUNDS` |
| `HOLE` | Fills reclaimed text inside live code so instruction walkers stay aligned; executing it is a runtime error. Format `[HOLE]` | |

`CHKB`, `CHKBN`, `CHKD` and `CHKP3` all resolve through the same `sorted_allocs_find` binary search (the largest tracked base at or below the pointer), which is how interior pointers work. `CHKB`'s bound is `AllocHeader.size`, and a negative offset is rejected only once it steps before the resolved allocation. `AllocHeader` exists only for VM-heap allocations, so `CHKB`, `CHKBN` and `CHKD` have no bound for a stack or global base; `CHKBS` covers fixed-size arrays and `CHKR` covers declared bounds, uniformly across heap, stack and global storage.

`CHKRO` shares `chkr_common()` with `CHKR` (`src/ops.c`), differing only in whether the sentinel short-circuits.

### Checked-pointer emission sites

| Opcode | Emitted from | Notes |
|---|---|---|
| `CHKNT` | The store path (`ND_ASSIGN` in `src/codegen_expr.c`), because it needs the stored value, which `gen_addr` never sees | A second site in `ND_CAS` checks an `_Atomic` element's *desired* value ahead of `ACAS`. `to_assign()` desugars `s[n] += 1` into a store carrying the same checked fields. `Obj.checked_prop_nt_elem` carries the terminator fact through propagation; `Node.checked_rmw_mirror` lets the propagation pass stamp desugared read-modify-write stores |
| `CHKNTZ` | The memcpy branch of `ND_ASSIGN`, right before `MCPY` | `float` and `double` stay on `CHKNT` via `FR2R`/`FR2R_F32` (so `-0.0f` traps). `long double` is excluded because the slot is 16 bytes but the store is an 8-byte `FSTR`. `checked_nt_pointee_supported()` (`src/parse_checked.c`) is the single gate |
| `CHKAB` | After a rewritten `q = E`, twice: against `q`'s declared lower bound, then its upper bound | Reused unmodified by the `dynamic` cast and by `rewrite_checked_call_args()` (call arguments become `(__ca = arg, __ca)` against a hidden checked local) |
| `CHKDC` | Only when `CCCC_CHECKED_BOUNDS` was set at parse time; otherwise the call parses to `ND_NULL_EXPR` like `__builtin_assume` | Never reaches native serialization in the default build |

`CHKNT` does not verify that a terminator is present: Checked C's `count(n)` is a lower bound, and finding the real terminator would need a read past `hi`.

### CHKT3

Emitted by `emit_load_ex`/`emit_store_ex` (`src/codegen_emit.c`) right after `CHKP3`, carrying the pointee's `TypeKind` and size. Operand `[rs:8|mode:8]`, trailing immediate `[(size<<8)|expected_type:64]`; `mode` is `CHKT3Mode` (`src/cccc.h`):

| Mode | Emitted by | Effect |
|---|---|---|
| `CHKT3_MODE_CHECK` (0) | A load | Compares the shadow across `[ptr, ptr+size)` with `expected_type` |
| `CHKT3_MODE_STAMP` (1) | A store | Sets `expected_type` as the effective type of the range |
| `CHKT3_MODE_CLEAR` (2) | A union member store | Erases the range, so a later access through another member does not false-positive |

Union member access is exempted both ways through `vm->compiler.in_union_member_access`: a union load emits no `CHKT3`, and a union store emits `CLEAR`. The shadow, its sparse pages and the host-call classification are in [safety-impl.md](safety-impl.md#type-shadow); the points specific to the opcodes:

| Topic | Detail |
|---|---|
| Shadows | `vm->heap_shadow` and `vm->data_shadow` (`TypeShadowSeg`); `type_shadow_locate` finds which segment a range lies in. For a heap address `op_CHKT3_fn` first confirms a live allocation (otherwise `CHKP3` reports); other addresses skip that and defer to `type_shadow_locate`, so the stack is untracked |
| Page vector | Grown lazily by `type_shadow_ensure` whenever `CCCC_TYPE_CHECKS` is set, so toggling it mid-run via `#pragma cccc config(safety=N)` stays in sync; a missing page reads as `TY_VOID` |
| Heap-mutating opcodes | `MFRE` clears; `MCPY` propagates source to destination; `REALC` carries the old block's shadow over; `MALC`, `CALC` and `MALCA` need no clear because an untouched range already reads `TY_VOID`; `RETBUF` clears its handed-out pool slot, since slots rotate between struct-returning calls |
| Guest `memcpy`/`memmove` | Shadow-aware shims (`cccc_shim_memcpy`/`cccc_shim_memmove`, `src/stdlib/string.c`) call `cc_type_shadow_copy`; this also covers heap-to-global copies |
| Other FFI calls | `op_CALLF_fn` clears reachable shadow before the call unless `ffi_shadow_classify`/`ffi_shadow_rules` (`src/ops.c`) classifies the name (table below) |
| `qsort` | `wrap_qsort` (`src/stdlib/stdlib.c`) is `HANDLED`: it checks `cc_type_shadow_elements_uniform` before and after the host call, and clears exactly `[base, base+nmemb*size)` if either fails |

| `FfiShadowClass` | Effect | Examples |
|---|---|---|
| `HANDLED` | No clear; the shim already propagated | `memcpy`, `memmove`, `qsort` |
| `READONLY` | No clear; never writes through a pointer | `strlen`, `strcmp`, `memcmp`, `fwrite`, `bsearch` |
| `BOUNDED` | Clear narrowed to `[args[out_arg], +len)`, clamped to the segment; other pointer arguments keep the default clear | `fread`, `snprintf`, `read`, `recv`, `strncpy` |
| `DEFAULT` | Whole-object clear (heap allocation, or pointer to the end of the data segment for a global) | Everything else, including `printf` and `scanf` (`%n`) |

A new rule can only reduce clearing relative to the default, so it cannot introduce a false positive.
