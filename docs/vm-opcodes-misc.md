# Misc Opcodes

Stack instrumentation, jumps, linking, FFI and traps.

## Stack Instrumentation Opcodes

| Opcode | Description |
|--------|-------------|
| `SCOPEIN` | Activate variables belonging to a lexical scope; records each variable's runtime liveness by actual address (bp+offset), not just its declaration record (#671) |
| `SCOPEOUT` | Deactivate variables (see #669 for why this no longer also aborts on tracked stack addresses — that check is detection-only, see `docs/safety.md`) |
| `CHKL` | Check variable liveness before access (use-after-scope / use-after-return), keyed by runtime address so cross-function offset collisions and recursive re-entry don't produce false positives (#671) |
| `MARKR` | Record a read access to a stack variable |
| `MARKW` | Record a write access; marks variable as initialised |

## Non-Local Jumps

| Opcode | Description |
|--------|-------------|
| `SETJMP` | Save `pc`, `sp`, `bp`, (and shadow-sp if CFI) into `jmp_buf`; return 0 |
| `LONGJMP` | Restore context from `jmp_buf`; return value in `REG_A1` (never 0) |

## Dynamic Linking

| Opcode | Description |
|--------|-------------|
| `DLOPEN` | `dlopen(path, mode)` — result in `REG_A0` |
| `DLSYM` | `dlsym(handle, symbol)` — result in `REG_A0` |
| `DLCLOSE` | `dlclose(handle)` — result in `REG_A0` |
| `DLERROR` | Return last dynamic-link error string in `REG_A0` |

## FFI

| Opcode | Description |
|--------|-------------|
| `CALLF` | Registered foreign-function call via `libffi`.  Operands: `ffi_idx`, `nargs`, `double_arg_mask` (2 words), `float_arg_mask` (2 words) — total 6 operand words |
| `CALLN` | Native-aware indirect call (dynamic symbol or FFI-token function pointer, falling back to a VM function).  Operands: `rs`, `meta` (bits 0-15 = nargs, bit 16 = returns_double, bit 17 = returns_float, bit 18 = is_variadic, bits 19-31 = fixed_param_count), `double_arg_mask` (2 words), `float_arg_mask` (2 words) — total 6 operand words. The variadic/fixed-param-count bits let the handler tell fixed flonum params (passed in `FREG_A0+`) apart from variadic-tail doubles (bit pattern in `REG_A0+`, matching the internal-call ABI so `va_arg` can read them after `ENT3` spills), and select the correct `libffi` prep (`ffi_prep_cif` vs `ffi_prep_cif_var`) for the callee |

### Declaring Libraries from Source

`extern` declarations are normally resolved against libraries passed on the
command line with `-l`/`--library`. `#pragma cccc link("name")` queues an
additional library from within the source file itself, merged into the same
list before FFI resolution:

```c
#pragma cccc link("m")          // -> libm.{dylib,so}/m.dll via the normal search
#pragma cccc link("libfoo.dylib") // an explicit filename is used as-is

extern double sqrt(double x);
```

The pragma accepts one or more comma-separated string literals (`link("a", "b")`)
and goes through the same `find_requested_library` / `cc_dlopen` /
`register_dynamic_externs` path as `-l`, so the requested libraries are also
passed to the system linker when compiling with `-c=native`.

When using `-E` (preprocessed output) or `-m`/`-c=generated` (serialized C
output), any queued libraries are re-emitted at the top of the output as
`#pragma cccc link("name")` — CCCC's own spelling, ignored as a harmless
unknown pragma by a plain downstream `cc` but parsed back into
`pragma_link_libs` if the output is ever re-fed to cccc itself, so the
requirement round-trips (#1149). `-c=native` emits neither spelling: the
library already reaches the host linker directly via the `-l` merge above,
so re-stating it as a pragma in the temporary source would be redundant.

## Bit-Manipulation Builtins

| Opcode | Description |
|--------|-------------|
| `CLZ` | Count leading zeros (`operand2` = 32 or 64) |
| `CTZ` | Count trailing zeros |
| `POPCOUNT` | Population count (width-agnostic, 64-bit) |
| `FFS` | Find first set bit (0 → 0) |
| `BSWAP` | Byte swap (`operand2` = 2, 4, or 8) |

## Checked Arithmetic

| Opcode | Description |
|--------|-------------|
| `ADDC` / `SUBC` / `MULC` / `DIVC` | Trapping signed arithmetic emitted for standard `+`, `-`, `*`, and `/` when `--overflow-checks` is enabled. |
| `IOVFL` | Overflow-checked `add`/`sub`/`mul`.  Inputs in `REG_A0`/`REG_A1`, result pointer in `REG_A2`; overflow boolean returned in `REG_A0`.  Operand encodes `op_type`, width, and signedness. |

## Struct Return Buffers

| Opcode | Description |
|--------|-------------|
| `RETBUF` | Return the next buffer from the rotating pool (for non-scalar returns) |

The pool itself (`alloc_return_buffer_pool`, `src/codegen_func.c`) is allocated in the data segment at `CCCC_MAX_DATA_ALIGN` (64-byte) alignment (#1136), covering the widest by-value return this convention carries — a 512-bit vector (#722).

A registered FFI cfunc (`cc_register_cfunc`) must honor this convention on
both ends, not just the host C ABI's own: a struct/union argument arrives as
a pointer to the *caller's own* storage (#714/#1078 — no scratch copy is
made for a struct/union the way there is for a vector/decimal arg, see the
by-value ABI writeups above), and a struct/union return must go back as a
pointer, not the host ABI's in-register struct return. Registering a raw
host symbol whose own C ABI takes or returns an aggregate by value (rather
than a small `wrap_*` marshalling function) silently breaks this — a guest
argument's address gets read as if it were the struct's own first word, or
REG_A0 gets treated as a struct pointer when it's actually raw return-register
bytes. Found auditing #1087: `inet_ntoa(struct in_addr)` (by-value argument)
and `div`/`ldiv`/`lldiv` (by-value return) were both registered raw; fixed
with wrapper functions (`wrap_inet_ntoa`, `wrap_div`/`wrap_ldiv`/`wrap_lldiv`)
that marshal explicitly, `src/stdlib/posix_net.c`/`stdlib.c` (#1090).

## Trap / Debugger

| Opcode | Description |
|--------|-------------|
| `BTRAP` | Unreachable / builtin trap.  Breaks into the debugger REPL when `-g` is active; otherwise prints a message and aborts.  Emitted for `__builtin_unreachable()`, `__builtin_trap()`, and `__builtin_debugtrap()`. |
| `VSIGNAL` | VM-managed `signal(sig, handler)`. Stores the per-signal action (DFL / IGN / VM function pointer) in `vm_sigslots`. Native dispositions are coordinated with the host-fault dispatcher so guest handlers and `SIG_IGN` take precedence without removing an active default crash trap. Returns the previous handler. |
| `VRAISE` | VM-managed `raise(sig)`. Delivers the signal synchronously from VM context: IGN is a no-op; DFL delegates to the host OS (and may enter the host-fault debugger); a VM handler is invoked by pushing the return address and jumping to the handler. `SIGTRAP` with `-g` enters the debugger REPL instead. |

## Fused Local Load / Store

These replace the common `LEA3 + LDR/STR` two-opcode sequence for local variables, eliminating one dispatch per scalar access.

| Opcode | Description |
|--------|-------------|
| `LDR_LOCAL_B` | `regs[rd] = *(char*)(bp + offset)` |
| `LDR_LOCAL_H` | `regs[rd] = *(short*)(bp + offset)` |
| `LDR_LOCAL_W` | `regs[rd] = *(int*)(bp + offset)` |
| `LDR_LOCAL_D` | `regs[rd] = *(long long*)(bp + offset)` |
| `STR_LOCAL_B` | `*(char*)(bp + offset) = regs[rd]` |
| `STR_LOCAL_H` | `*(short*)(bp + offset) = regs[rd]` |
| `STR_LOCAL_W` | `*(int*)(bp + offset) = regs[rd]` |
| `STR_LOCAL_D` | `*(long long*)(bp + offset) = regs[rd]` |
| `FLDR_LOCAL` | `fregs[rd] = *(double*)(bp + offset)` |
| `FSTR_LOCAL` | `*(double*)(bp + offset) = fregs[rd]` |
| `FLDR_LOCAL_F32` | `fregs[rd] = *(float*)(bp + offset)` |
| `FSTR_LOCAL_F32` | `*(float*)(bp + offset) = fregs[rd]` |
