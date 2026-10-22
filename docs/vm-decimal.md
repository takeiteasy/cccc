# Decimal Floating-Point

How `_Decimal32/64/128` work in the VM: representation, opcodes, formatting, rounding and folding.

## Build and availability

`_Decimal32/64/128` (C23) use IEEE-754-2008 decimal encoding via the Intel BID library, opt-in with `make CCCC_HAS_DECIMAL=1`. The library is never vendored: `tools/fetch_intel_bid.sh` downloads it (or reuses a tarball), verifies its SHA3-256 and builds `libbid.a` into the gitignored `build/intel-bid/`.

```sh
tools/fetch_intel_bid.sh
make CCCC_HAS_DECIMAL=1 CCCC_BID_PREFIX=build/intel-bid
```

| Without the flag | Behaviour |
|---|---|
| Declarations, `sizeof`, `_Alignof`, typedefs, struct and array layout | Work (`TY_DECIMAL32/64/128`, 4/8/16 bytes) |
| Decimal literals and arithmetic | A clean compile error |

A guest tells the configurations apart with the `__STDC_IEC_60559_DFP__` predefine (and the legacy `__STDC_DEC_FP__`).

## Representation

A decimal value is **address-based**, like a small struct or a wide `_BitInt(N>64)`: a pointer to a 4/8/16-byte BID buffer in an ordinary integer register, never in `fregs[]` or `vregs[]`. Temporaries come from the per-function scratch pool wide `_BitInt` uses (`alloc_decimal_temp`, whole 64-bit words). Struct members, array elements and locals are sized from `ty->size`.

## Opcodes

All twelve take **zero operand words and fixed argument registers** `REG_A0`..`REG_A3`, like the `WIDE_ADD`/`WIDE_SUB` family; arguments are loaded beforehand with `MOV3`/`LEA3`/`LI3`. `width` is 0 = `_Decimal32`, 1 = `_Decimal64`, 2 = `_Decimal128`.

| Opcode | Arguments | Effect |
|---|---|---|
| `DADD`, `DSUB`, `DMUL`, `DDIV` | `A0`=dst, `A1`=a, `A2`=b (addresses), `A3`=width | `*dst = *a op *b` |
| `DNEG` | `A0`=dst, `A1`=a, `A2`=width | `*dst = -*a` |
| `DCMP` | `A0`=a, `A1`=b, `A2`=width | `A0` = 0 EQ, 1 LT, 2 GT, 3 UNORDERED |
| `DFROMI` | `A0`=dst, `A1`=int64, `A2`=width, `A3`=is_unsigned | `*dst = (decimal)value` |
| `DTOI` | `A0`=src, `A1`=width, `A2`=is_unsigned | `A0` = `(int64)*src`, truncating |
| `DFROMBITS` | `A0`=dst, `A1`=raw f32/f64 bits, `A2`=width, `A3`=src_is_f32 | `*dst = (decimal)binary` |
| `DTOBITS` | `A0`=src, `A1`=width, `A2`=dst_is_f32 | `A0` = bit pattern of `(binary)*src` |
| `DCVT` | `A0`=dst, `A1`=src, `A2`=dst_width, `A3`=src_width | Decimal to decimal |
| `DFMT` | `A0`=buf, `A1`=n, `A2`=val, `A3`=width | `A0` = bytes that would be written (`snprintf` contract) |

`DFROMBITS` and `DTOBITS` move the binary side as raw bits through `FR2R`/`FR2R_F32` and `R2FR`/`R2FR_F32`, so no decimal opcode touches an `FReg`. `DCMP`'s four-way result lets `==`, `!=`, `<` and `<=` stay correct with a NaN operand; `>` and `>=` become swapped `<` and `<=` at parse time. `__builtin_decimal_to_chars(buf, n, d)` lowers to `DFMT` and always gives BID's canonical shortest form.

**Clobber contract.** Every `D*` and `WIDE_*` handler reads and writes only `REG_A0`..`REG_A5` and never a `T` register, and codegen relies on it: the decimal and wide `_BitInt` binop branches stage one operand's address in a `T` register across the other operand's evaluation, and these opcodes must not reset the temp allocator as a real call does.[^reset] The wide `AND`/`OR`/`XOR`/compare ops, and any decimal op reached through a real FFI call (`emit_wide_helper`'s `CALLF`), do clobber caller-saved `T` registers and reset the allocator.

## printf and scanf

`%Hf`, `%Df` and `%DDf` (`_Decimal32/64/128`) work with `f F e E g G`, flags (`- + space # 0`), width and precision.

| Entry point (`src/stdlib/decimal.c`) | Role |
|---|---|
| `cccc_dec_format_ex(buf, n, val, width, conv, flags, field_width, prec)` | printf side: decomposes via `__bidNN_to_string` into sign, digits and exponent, rounds half-even (BID's default, consistent with `+ - * /`), and renders from that triple, never through a binary `double`, so it is exact beyond what `long double` holds |
| `cccc_dec_from_string(width, dst, s)` | scanf side: wraps `__bidNN_from_string`, the entry point compile-time literals use (`cccc_dec_encode_literal`), so parsing is exact |

No host libc formats decimals (checked against glibc 2.39), so a `CCCC_HAS_DECIMAL=1` build always sends `printf`, `fprintf`, `sprintf` and `snprintf` through the custom `stb_sprintf`-derived engine (`src/stdlib/format_printf.c`), as the `scanf` family already is.

### Variadic ABI

A decimal in a variadic tail is passed **by pointer** to a caller-frame scratch copy (`gen_decimal_arg_ptr`, `src/codegen_emit.c`), like vectors. Decimals have no default promotion, which is why three length modifiers exist, and the by-pointer form keeps all three widths distinct in one 8-byte slot. `<stdarg.h>`'s `va_arg` finds the case through `__builtin_classify_type`, with `CCCC_DECIMAL_TYPE_CLASS = 98` beside vector's `99`.

| Mismatch | Result |
|---|---|
| `%DDf` reading a `_Decimal32` | Memory-safe: the scratch copy is always the full 16 bytes and 16-byte aligned, so it reads uninitialized but in-bounds bytes |
| `%Df` given a plain `double` | Crash: the `double`'s bits are dereferenced as a pointer, as `%s` given a `double` already does. The dereference happens host-side in `cccc_dec_format_ex`, so even `-3` cannot catch it |

## Math and parsing

| Feature | How |
|---|---|
| `sqrtd64`, `expd128`, ... (`<decimal_math.h>`, `CCCC_HAS_DECIMAL=1`; list in [coverage.md](coverage.md)) | No new opcodes. An ordinary stdlib module (`src/stdlib/decimal_math.c`, registered through `tools/stdlib.tsv`) reached by the normal FFI path through six entry points taking raw byte addresses |
| `strtod32/64/128` | `static inline` wrappers in `include/stdlib.h` over one `long long`-uniform FFI entry, `__cccc_dec_strtod` (`src/stdlib/stdlib.c`) to `cccc_dec_strtod`. It scans the longest numeric prefix itself (BID gives no `endptr`) and sizes its own copy, so long coefficients never truncate; `*endptr` and the no-conversion case follow C's `strtod` |

A decimal is returned by value from a guest `static inline` function through the struct-ABI reuse below, so no decimal crosses the FFI boundary itself.

## Rounding and exception flags

Decimal arithmetic honours `<fenv.h>`.

| Direction | Mapping |
|---|---|
| Rounding | `cccc_dec_host_rounding()` (`decimal.c`) and `dm_host_rounding()` (`decimal_math.c`) map `fegetround()` to `BID_ROUNDING_*` (`FE_TONEAREST`, `_DOWNWARD`, `_UPWARD`, `_TOWARDZERO` to `TO_NEAREST`, `_DOWN`, `_UP`, `_TO_ZERO`), as `__cccc_flt_rounds` does. `BID_ROUNDING_TIES_AWAY` has no portable equivalent and is unreachable |
| Flags | `cccc_dec_raise_flags()` and `dm_raise_flags()` map BID's invalid, divide-by-zero, overflow, underflow and inexact bits (identical across platforms) to host `FE_*` and call `feraiseexcept()`. `DEC_FE_UNNORMAL` has no `FE_*` equivalent and is masked |

Entry points that can round or raise take a trailing `env` (`src/internal.h`):

| `env` | Policy | Used by |
|---|---|---|
| `CCCC_DEC_ENV_DYNAMIC` | Host rounding mode; flags raised | Every `src/ops.c` `D*` handler and the runtime `strtod`/scanf paths |
| `CCCC_DEC_ENV_STATIC` | Round to nearest; flags discarded | The compile-time folder (`eval_decimal`, `src/parse_expr.c`), which runs in the compiler process and must not perturb its FP environment |

`cccc_dec_neg` and `cccc_dec_cmp` take no `env` (exact and quiet). `cccc_dec_to_int` truncates (C cast semantics) but raises flags. `cccc_dec_format` and `_ex` stay nearest-rounding and flag-free: `printf` need not raise, and its precision rounding is specified half-even.

### Compile-time evaluation must not leak into runtime FP state

| Source | Protection |
|---|---|
| A comptime macro calling a decimal `<math.h>` function (reaches `decimal_math.c` directly, which has no `env`) | `fenv_barrier_begin`/`_end` (`src/macros.c`) around the comptime `vm_eval()`, at the expression-position call and `__builtin_comptime_init` sites |
| `eval_decimal`, used for decimal static initializers and the decimal-to-float/int cast arms of `eval_double`/`eval2` | Wraps its own entry the same way |

The barriers save and restore only the rounding mode and then `feclearexcept()`; they do not round-trip flags through `fegetenv()`/`fesetenv()`. The compile phase can already leave flags dirty before any decimal code runs (`convert_pp_number` in `tokenize.c` calls a host `strtold()` on every float literal, and `strtold("1.1", NULL)` alone sets `FE_UNDERFLOW` on at least one platform), and restoring a saved environment would bring that back. The guest's clean start comes from `cc_run()` (`src/vm.c`), which resets rounding to nearest and clears all flags once, first, before constructors, `main` or `atexit` handlers. A constructor's own `fesetround()` and `feraiseexcept()` calls still persist into `main`, as in linked C.

## Constant folding

`eval_decimal` (`src/parse_expr.c`) folds `+ - * /`, unary `-`, `?:`, `,`, decimal-to-decimal casts and casts to and from integer and binary float, so `static _Decimal64 x = 1.1dd + 2.2dd;` works, mirroring `eval_double` and `eval2`. Each call takes its width from the *node's* type (`dec_width_code`); `usual_arith_conv` should have inserted a cast wherever widths meet, so a mismatch means a missing cast.

Folding decimal-to-integer casts makes `(int)1.5dd` valid in array bounds and case labels. `(int)(1.1dd + 2.2dd)` also folds, a GCC-compatible extension: strict C allows a floating constant only as a cast's immediate operand.

Not yet folded: decimal comparisons in an integer constant expression (`_Static_assert(1.1dd == 1.1dd, "")`), which needs decimal arms in `eval2`'s `ND_EQ`, `ND_NE`, `ND_LT` and `ND_LE`.

## ABI

| Case | Convention |
|---|---|
| By-value argument or return | Reuses the vector struct-ABI: a caller-frame scratch-slot copy for arguments, `RETBUF` plus a copy for returns |
| `CALLT` (tail call) | Excluded for any call with a decimal argument or return: the scratch slot lives in a frame the tail call would reuse, leaving a dangling pointer |
| Variadic tail | By pointer ([above](#variadic-abi)) |
| Fixed parameter or return through a native FFI call | Rejected with a diagnostic: `libffi` has no decimal `ffi_type`, so there is no by-value marshalling yet |

[^reset]: `emit_wide_op` (`src/codegen_emit.c`) no longer calls `reset_temp_regs()`. No `op_*_fn` handler for a `D*` or `WIDE_*` opcode (`src/ops.c`) touches a `REG_T*` slot, and the reset was unsound: it silently freed a live temp belonging to an enclosing expression still in progress.
