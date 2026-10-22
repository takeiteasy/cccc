# Inline Assembly

Inline `asm` is a no-op in the VM unless you pass `--asm-passthru`. Under
`-c=native` the statement is emitted verbatim and the host compiler handles it.

| Mode | `asm("nop")` | Extended `asm("..." : outs : ins : clobbers)` |
|---|---|---|
| VM (default) | skipped | skipped; outputs keep their value, `asm goto` never jumps (`-Wignored-features`) |
| VM + `--asm-passthru` | runs on the host CPU | runs on the host CPU, reading and writing VM variables |
| `-c=native`, `-m`, `-c=generated` | emitted verbatim | emitted verbatim |

## File-scope asm

`asm("...");` outside any function emits assembler text into the output, for
example to define a symbol or embed a file with `.incbin`.

```c
__asm__(".globl answer\nanswer: .byte 42\n");
```

| Mode | Behaviour |
|---|---|
| VM (default) | skipped (`-Wignored-features`) |
| VM + `--asm-passthru` | assembled into the host library; the VM cannot reference its symbols |
| `-c=native`, `-m` | emitted verbatim as `__asm__("...");` |
| `-c=generated` | not emitted; the original source already carries it |

Only a single string literal is accepted; operands and qualifiers are
function-scope only.

## Running asm in the VM

`--asm-passthru` (`-A`) compiles each asm statement with the host C compiler
(`CCCC_NATIVE_CC`, else `cc`) and calls it from the VM. All statements of one
compile share a single host build.

```c
int main(void) {
    int in = 41, out = 0;
    __asm__ volatile("add %w0, %w1, #1" : "=r"(out) : "r"(in));  // aarch64
    return out;                                                  // 42
}
```

```
cccc --asm-passthru demo.c
```

Templates, constraints, `[names]`, qualifiers and clobbers pass to the host
compiler unchanged, so they use the host's syntax. Host compiler errors
(a constraint it rejects, a bad template) appear on stderr with your source
file and line, followed by `native compilation failed`.

## Operands

| Operand | How it reaches the asm |
|---|---|
| Output (`=r`, `+r`, `=m`), memory input | by address of the C lvalue, so the asm writes the variable directly |
| Integer constant input (`"i"(41)`) | as a literal |
| Other scalar input (`"r"(x + 2)`) | by value, from a temporary |
| Array input with a register constraint (`"r"(arr)`) | as a pointer to the first element |
| Struct, union or array with a memory constraint (`"m"(s)`) | by address |
| Bit-field output (`"=r"(s.f)`, `"+r"(s.f)`) | through a temporary: seeded for `+`, stored back into the bit-field after the asm |
| Bit-field input (`"r"(s.f)`) | by value |

Supported scalar types are integers, `_Bool`, enums, pointers, `float` and
`double`. Everything else is rejected with an error:

| Rejected | Reason |
|---|---|
| `asm goto` | control cannot jump from host code back into the VM |
| `long double`, `_Complex`, vectors, `_BitInt`, `_Decimal`, 128-bit integers | no host spelling for the VM's value |
| Struct or union with a register constraint | the value would be a copy; use a memory constraint |
| Bit-field with a memory constraint (`"m"(s.f)`) | a bit-field has no address |

The `"memory"` clobber holds as written: the VM keeps every variable in memory
and never caches a value across statements.

Custom `cc_set_asm_callback` callbacks still receive the template only.
