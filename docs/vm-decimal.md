# Decimal Floating-Point

The BID-backed decimal runtime.

## Decimal Floating-Point

`_Decimal32/64/128` (C23) use real IEEE-754-2008 decimal encoding via the
Intel BID library, opt-in with `make CCCC_HAS_DECIMAL=1`. The library is
never vendored: `tools/fetch_intel_bid.sh` downloads (or reuses an
already-present tarball), verifies its SHA3-256, and builds `libbid.a` into
a gitignored `build/intel-bid/` prefix.

```
tools/fetch_intel_bid.sh
make CCCC_HAS_DECIMAL=1 CCCC_BID_PREFIX=build/intel-bid
```

Without the flag, `_Decimal32/64/128` declarations, `sizeof`, `_Alignof`,
typedefs, and struct/array layout all work normally (they have their own
`TypeKind` values, `TY_DECIMAL32/64/128`, sized 4/8/16 bytes); only decimal
*literals and arithmetic* require the library, and are a clean compile
error without it. A guest program distinguishes the two configurations via
the `__STDC_IEC_60559_DFP__` predefine (also exposed as the legacy
`__STDC_DEC_FP__` spelling).

### Value representation

A decimal value is **address-based**, like a small struct or a wide
`_BitInt(N>64)` value — never routed through `fregs[]`/`vregs[]`. This is
what lets `_Decimal128` (16 bytes) work with no special case: every width
is "a pointer to a 4/8/16-byte BID buffer" carried in an ordinary integer
register. Local/temporary storage comes from the same per-function scratch
stack pool wide `_BitInt` uses (`alloc_decimal_temp`, rounding up to whole
64-bit words). Struct members, array elements, and function locals get
their storage sized directly from `ty->size` like any other type.

### Opcodes

All twelve decimal opcodes share one convention: **zero operand words,
fixed argument registers** (`REG_A0`–`REG_A3`), identical in shape to the
`WIDE_ADD`/`WIDE_SUB`/… family (#456) used for wide `_BitInt`. Arguments are
loaded into place with ordinary `MOV3`/`LEA3`/`LI3` immediately beforehand;
the opcode itself just performs the operation. No decimal op is ever
RRRS-operand-decoded.

`width` is `0` = `_Decimal32`, `1` = `_Decimal64`, `2` = `_Decimal128`.

| Opcode | Arguments | Effect |
|--------|-----------|--------|
| `DADD` | `A0`=dst addr, `A1`=a addr, `A2`=b addr, `A3`=width | `*dst = *a + *b` |
| `DSUB` | same | `*dst = *a - *b` |
| `DMUL` | same | `*dst = *a * *b` |
| `DDIV` | same | `*dst = *a / *b` |
| `DNEG` | `A0`=dst addr, `A1`=a addr, `A2`=width | `*dst = -*a` |
| `DCMP` | `A0`=a addr, `A1`=b addr, `A2`=width | `A0` result: `0`=EQ, `1`=LT, `2`=GT, `3`=UNORDERED |
| `DFROMI` | `A0`=dst addr, `A1`=int64 value, `A2`=width, `A3`=is_unsigned | `*dst = (decimal)value` |
| `DTOI` | `A0`=src addr, `A1`=width, `A2`=is_unsigned | `A0` = `(int64)*src`, truncating (C semantics) |
| `DFROMBITS` | `A0`=dst addr, `A1`=raw f32/f64 bits, `A2`=width, `A3`=src_is_f32 | `*dst = (decimal)binary_value` |
| `DTOBITS` | `A0`=src addr, `A1`=width, `A2`=dst_is_f32 | `A0` = raw f32/f64 bit pattern of `(binary)*src` |
| `DCVT` | `A0`=dst addr, `A1`=src addr, `A2`=dst_width, `A3`=src_width | `*dst = (decimal)*src` (decimal-to-decimal) |
| `DFMT` | `A0`=buf, `A1`=n, `A2`=val addr, `A3`=width | `A0` = bytes that would have been written (`snprintf` contract) |

`DFROMBITS`/`DTOBITS` move the binary float side as a raw bit pattern via
`FR2R`/`FR2R_F32` and `R2FR`/`R2FR_F32` (the existing float-register
bit-reinterpret opcodes) rather than through memory — no decimal opcode
ever touches an `FReg` directly. `DCMP`'s four-way result (rather than a
plain `-1/0/1`) is what lets `==`/`!=`/`<`/`<=` all be derived correctly
even when one operand is NaN; `>`/`>=` never reach codegen as distinct
opcodes — like binary float, they're normalized to a swapped `<`/`<=` at
parse time.

**Clobber contract (#838):** every `D*` opcode and every `WIDE_*` opcode
reads/writes only `REG_A0`–`REG_A5` and the fixed-A-register block's opaque
side effects (`emit_wide_op`, `src/codegen_emit.c`) — none of them touch a `T`
register, and codegen relies on this: the decimal and wide-`_BitInt` binop
branches stage one operand's address across the other operand's evaluation
in a `T` register, which would be silently clobbered if these opcodes reset
the temp-register allocator the way a real call does. This differs from
`AND`/`OR`/`XOR`/comparison wide-`_BitInt` ops and any decimal op reached
through a genuine FFI call path (`emit_wide_helper`'s `CALLF`), which *do*
clobber caller-saved `T` registers like any other call and must reset the
allocator accordingly. Before #838, `emit_wide_op` also called
`reset_temp_regs()` defensively; that was removed because no `op_*_fn` handler
for a `D*`/`WIDE_*` opcode (`src/ops.c`) ever reads or writes a `REG_T*` slot,
and the reset was unsound — it silently freed a live temp belonging to an
*enclosing* stack frame's still-in-progress expression, not just the current
one.

`__builtin_decimal_to_chars(buf, n, decimal_value)` lowers directly to `DFMT`
and always produces BID's canonical shortest-form string (no flags, width, or
precision — that's its whole contract).

### `printf`/`scanf` integration (#829)

`%Hf`/`%Df`/`%DDf` (`_Decimal32`/`_Decimal64`/`_Decimal128`) work with the
`f`/`F`/`e`/`E`/`g`/`G` conversions, including flags (`- + space # 0`), field
width, and precision — the same surface as a binary `double`. Two runtime
shim entry points do the work (`src/stdlib/decimal.c`, declared in
`src/internal.h`, both raw-byte-pointer like the arithmetic shim above):

- `cccc_dec_format_ex(buf, n, val, width, conv, flags, field_width, prec)` —
  printf side. Decomposes the value via `__bidNN_to_string` into a
  `(sign, digit-string, decimal exponent)` triple, rounds it to the requested
  precision with round-half-even (BID's own default rounding mode, so this
  stays consistent with `+`/`-`/`*`/`/`), and renders fixed/scientific/general
  form from that triple directly — never through a binary `double`
  intermediate, so it's exact for values a `long double` can't represent
  (verified for `_Decimal128`).
- `cccc_dec_from_string(width, dst, s)` — scanf side. Wraps
  `__bidNN_from_string`, the same entry point compile-time decimal literals
  use (`cccc_dec_encode_literal`), so parsing is exact rather than routing
  through `strtold`.

No native host libc implements decimal floating-point conversions (verified
against real glibc 2.39: `printf("%Df", ...)` prints the length modifier and
conversion character literally rather than formatting the argument), so a
`CCCC_HAS_DECIMAL=1` build always routes `printf`/`fprintf`/`sprintf`/
`snprintf` through the custom `stb_sprintf`-derived engine
(`src/stdlib/format_printf.c`) regardless of `CCCC_HAVE_NATIVE_PCT_B` — the
same reasoning that already makes the `scanf` family route through the custom
engine unconditionally (#728).

**Variadic ABI.** A decimal argument in the variadic tail of a call — the
`x` in `printf("%Df", x)` — is passed **by pointer** to a caller-frame
scratch copy (`gen_decimal_arg_ptr`, `src/codegen_emit.c`), mirroring the
vector-variadic convention (#721, see the "ABI" subsection below).
`_Decimal32/64/128` isn't
subject to default argument promotion (unlike `float`→`double`), which is
why three distinct length modifiers exist; the by-pointer convention keeps
all three widths distinct through a single 8-byte variadic slot regardless
of which one is used. The scratch copy is
always allocated at the full 16 bytes / 16-byte alignment, independent of
the argument's actual 4/8/16-byte width: `validate_format_call`'s
`%Hf`/`%Df`/`%DDf` type-checking only covers a literal format string and
only warns, so a width mismatch **between two decimal widths** (`%DDf`
reading a `_Decimal32`) must stay memory-safe — it reads
uninitialized-but-in-bounds bytes from a fully-sized object rather than
overrunning a narrower one. A mismatch **between a decimal and a
non-decimal argument** (`%Df` given a plain `double`) is a different,
unfixable case: it dereferences the `double`'s bit pattern as a pointer and
crashes, exactly the same real UB `%s` given a `double` already has today
with any pointer-expecting conversion — the by-pointer decimal ABI doesn't
introduce a new hazard class here, though it does make the *consequence* of
that specific mismatch a crash rather than garbage output (GCC's decimal
varargs are by-value, so the equivalent GCC mismatch just misreads bits).
Not something `-3`'s dangling-pointer detection can catch either: the
dereference happens host-side inside `cccc_dec_format_ex`/`__bidNN_to_string`
(via `va_arg(va, void *)` in `stb_sprintf.h`'s decimal short-circuit),
outside the VM's own instrumentation. `<stdarg.h>`'s `va_arg`
detects the by-pointer case via `__builtin_classify_type`, extended with a
second discriminant (`CCCC_DECIMAL_TYPE_CLASS = 98`, alongside vector's
`99`) folded into the same by-pointer `va_arg` arm.

### `<math.h>` transcendentals (#828)

`sqrt`/`exp`/`log`/`pow`/`sin`/`cos`/... for `_Decimal32/64/128` (`<decimal_math.h>`,
opt-in with `CCCC_HAS_DECIMAL=1`; see [coverage.md](coverage.md)'s C23 header
table for the full function list) add **no new opcodes**. Unlike the twelve
`D*` arithmetic opcodes above — which call directly into `src/stdlib/decimal.c`
from `src/ops.c` and are never FFI-registered — decimal maths is an ordinary
stdlib module (`src/stdlib/decimal_math.c`, registered like any other
`<header.h>` → `register_*_functions` mapping in `tools/stdlib.tsv`). A guest
call to e.g. `sqrtd64()` goes through the normal FFI call path into six
op-dispatch entry points, each taking raw byte addresses (same "no BID type in
a VM header" convention as the `cccc_dec_*` shim functions above) rather than
through a dedicated opcode.

### `strtod32`/`strtod64`/`strtod128` (#832)

Parsing a decimal from a *runtime* string (as opposed to a compile-time
literal) adds **no new opcode**, following the `<math.h>` precedent directly
above: `include/stdlib.h`'s `strtod32`/`strtod64`/`strtod128` are `static
inline` wrappers over a single `long long`-uniform FFI entry point,
`__cccc_dec_strtod` (`src/stdlib/stdlib.c`), which forwards to
`cccc_dec_strtod` (`src/stdlib/decimal.c`). `cccc_dec_strtod` scans the
longest valid numeric prefix itself — `__bidNN_from_string` gives no
`endptr` — sizing its own copy to the scanned length rather than a fixed
buffer, so an arbitrarily long coefficient never truncates. `*endptr` and the
no-conversion (`+0`, `endptr == s`) case match C's `strtod()` contract.

Decimal **return by value** from a guest `static inline` function is already
supported (the struct-ABI reuse described below) — this is exactly what
every `<decimal_math.h>` wrapper already does, so no decimal value ever
crosses the FFI boundary itself. `#830`'s restriction (decimal rejected as a
*fixed* FFI parameter or return) doesn't apply here for that reason.

### `<fenv.h>` rounding and exception flags (#832)

Phase 1 hard-wired every arithmetic/conversion entry point in
`src/stdlib/decimal.c` and `src/stdlib/decimal_math.c` to
`BID_ROUNDING_TO_NEAREST` and discarded BID's exception-flags out-parameter,
so `fesetround()` had no effect on decimal arithmetic and a decimal overflow/
underflow/inexact/invalid/divide-by-zero could never be observed via
`fetestexcept()`. Both are now real:

- **Rounding.** `cccc_dec_host_rounding()` (`decimal.c`) / `dm_host_rounding()`
  (`decimal_math.c`) map the host's *current* `fegetround()` mode to the
  matching `BID_ROUNDING_*` constant (`FE_TONEAREST`/`_DOWNWARD`/`_UPWARD`/
  `_TOWARDZERO` → `BID_ROUNDING_TO_NEAREST`/`_DOWN`/`_UP`/`_TO_ZERO`) — the
  same mapping `__cccc_flt_rounds` (`src/stdlib/fenv.c`) already used for
  `FLT_ROUNDS`. `BID_ROUNDING_TIES_AWAY` has no portable `<fenv.h>`
  equivalent and is unreachable (follow-up ticket).
- **Exception flags.** `cccc_dec_raise_flags()` / `dm_raise_flags()` map
  BID's `DEC_FE_INVALID`/`_DIVBYZERO`/`_OVERFLOW`/`_UNDERFLOW`/`_INEXACT`
  bits (verified numerically identical across platforms) to the matching
  host `FE_*` bits and call `feraiseexcept()`. `DEC_FE_UNNORMAL` (denormal)
  has no portable `FE_*` equivalent and is masked out.

Every entry point that can round or raise takes a trailing `env` parameter
(`CCCC_DEC_ENV_DYNAMIC` or `_STATIC`, `src/internal.h`):

- **`CCCC_DEC_ENV_DYNAMIC`** — the runtime policy above (host rounding mode,
  flags raised). Used by every `src/ops.c` `D*` opcode handler and by the
  runtime `strtod32/64/128`/scanf paths.
- **`CCCC_DEC_ENV_STATIC`** — always round-to-nearest, flags discarded. Used
  only by the compile-time constant folder (`src/parse_expr.c`'s `eval_decimal`,
  see below), which runs inside the *compiler* process and must never
  observe or perturb the host FP environment it happens to be in.

`cccc_dec_neg`/`cccc_dec_cmp` take no `env` (negation is exact; the quiet
comparisons cannot raise). `cccc_dec_to_int` keeps `__bidNN_to_int64_int`
(truncation *is* C cast semantics regardless of rounding mode) but still
raises flags. `cccc_dec_format`/`cccc_dec_format_ex` (printf) deliberately
stay flag-discarding and nearest-rounding — `printf` isn't required to raise
FP exceptions, and its precision rounding is independently specified as
round-half-even.

**Compile-time evaluation must never leak into the guest's runtime FP
state**, in either direction — the trap this design exists to close:

- A comptime macro (`[[cccc::comptime]]`) calling a decimal `<math.h>`
  function reaches `src/stdlib/decimal_math.c` directly (no `env`
  parameter there at all — it's FFI-only, always dynamic), so it depends on
  a save/restore barrier around the comptime `vm_eval()` call
  (`src/macros.c`'s `fenv_barrier_begin`/`_end`, wrapping both the
  expression-position macro-call site and the `__builtin_comptime_init`
  site) to avoid leaving the *compiler's* rounding mode or exception flags
  dirty after that macro runs.
- `eval_decimal` (`src/parse_expr.c`) — the folder used by `write_gvar_data` for
  a decimal-typed static/global initializer, and by `eval_double`/`eval2`'s
  `ND_CAST` arms for the reverse decimal→binary-float/decimal→int
  directions — wraps its own top-level entry the same way.

Both barriers save/restore only the **rounding mode**, not the whole
exception-flag state, and explicitly `feclearexcept()` before returning —
deliberately *not* round-tripping the flags through `fegetenv()`/`fesetenv()`.
This survives an independently-verified pre-existing condition: the
*compile* phase can already leave host FP exception flags dirty before any
decimal code runs at all (`tokenize.c`'s `convert_pp_number` scans every
floating/decimal literal's extent via a host `strtold()` call whose value is
discarded but whose side effect isn't — `strtold("1.1", NULL)` alone sets
`FE_UNDERFLOW` on at least one verified platform) — restoring a saved
environment via `fesetenv()` would silently reintroduce that dirty state.
The guest program's actual clean-start guarantee comes from a third,
higher-level fix: `cc_run()` (`src/vm.c`) resets the host FP environment
(`fesetround(FE_TONEAREST); feclearexcept(FE_ALL_EXCEPT);`) exactly once, as
the very first thing it does — before constructors, `main()`, or `atexit`
handlers run (each of which is its own separate `cc_run_at()` cycle). A
constructor's own `fesetround()`/`feraiseexcept()` calls still correctly
persist into `main()` and beyond, matching real linked-C behavior, since the
reset happens only once at the top, not before every `cc_run_at()` cycle —
while whatever the *compile* phase left dirty never reaches the guest at
all.

### Compile-time constant folding (#832)

`static _Decimal64 x = 1.1dd + 2.2dd;` used to be a hard diagnostic — only a
bare literal (or a cast of one) was accepted as a decimal static/global
initializer. `eval_decimal` (`src/parse_expr.c`) now folds `+ - * /`, unary `-`,
`?:`, `,`, a decimal-to-decimal cast, and casts to/from integer and binary
floating-point, mirroring the shape of the existing `eval_double`/`eval2`
integer/binary-float constant folders. Each recursive call derives its
working width from the *node's own* type (`dec_width_code`), never trusts a
caller-supplied width beyond the final store — `usual_arith_conv` is
expected to have inserted a cast wherever two different decimal widths
meet, so a width mismatch indicates a missing cast rather than something
safe to paper over. Folding a decimal-to-integer cast is what makes
`(int)1.5dd` in an array bound, case label, or similar integer constant
expression fold correctly (`(int)(1.1dd + 2.2dd)` also folds, a
GCC-compatible extension: strict C only permits a floating constant as a
cast's *immediate* operand, which `1.5dd` satisfies and the sum doesn't).

**Still deferred** (follow-up ticket): decimal comparisons directly in an
integer constant expression (`_Static_assert(1.1dd == 1.1dd, "")`) — that
needs decimal arms in `eval2`'s `ND_EQ`/`ND_NE`/`ND_LT`/`ND_LE`, which
folding static initializers didn't require.

### ABI

By-value decimal arguments and returns reuse the vector-by-value struct-ABI
machinery (#714): a caller-frame scratch-slot copy for arguments, `RETBUF`
+ copy for returns. `CALLT` (tail-call optimization) is excluded for any
call with a decimal return *or* a decimal argument, for the same frame-reuse
hazard reason vector returns/args are (#716) — the value's scratch slot
lives in the caller's or callee's own frame, so a tail call reusing that
frame would return or read through a dangling pointer.

A decimal value through the **variadic tail** of a call is passed by
pointer (#829, see above) — this covers `printf`/`scanf` and any other
variadic call whose receiving side reads it back via `va_arg`. A decimal
value as a **fixed** parameter or return through a **native FFI call**
remains rejected with a diagnostic: `libffi` has no decimal `ffi_type`, so
neither has a sound by-value marshalling convention yet (tracked as #830).
