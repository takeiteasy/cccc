# Native Output Implementation Notes

Contributor notes behind [native.md](native.md) and [headers-native.md](headers-native.md).

## Replayed includes

- **Chains.** A bodiless declaration sourced from a captured chain of ordinary includes is not re-derived. One reached through *another bundled* header (bundled `fcntl.h` including `unistd.h`) is still re-derived, because the replayed `<fcntl.h>` resolves to the host's own copy. Re-derived declarators are parenthesized (`int (getc_unlocked)(FILE *stream);`) so a function-like macro of the same name does not expand, and a pointer type that is a from-include typedef keeps its alias spelling.
- **Expansion site decides.** Whether a header supplies a function body, a bodiless declaration or a global is keyed on where the text is written (the macro expansion site), not on the spelling location of a token-pasted name. A macro in a captured header that defines something only when invoked elsewhere is attributed to the invoking input.
- **Repeated library includes.** The replayed output shares one macro namespace. The surviving `#include` goes at the *last* input's position when a captured `#define`/`#undef` sits between occurrences, so every configuration macro is live, and at the first occurrence otherwise. An earlier input defining a colliding configuration macro after its own include would see it applied twice; no real usage does this.

## Host-owned layouts under self-hosting

When cccc compiles its own `src/stdlib/*.c` as guest input, a bundled header whose real host layout differs from CCCC's projection (`sys/mount.h` `struct statfs`, `sys/statvfs.h` `struct statvfs`, `fenv.h` `fenv_t`) needs a `#ifdef __CCCC__` / `#include_next` hand-off. The `-idirafter` demotion covers user `-I` entries only.

Type layout re-materializes as source text (`sizeof(struct statfs)`) and the host compiler resolves it, so it self-corrects. A numeric macro value (`_SC_PAGESIZE`, `POLLWRBAND`) is folded into the AST at preprocess time and needs the `__CCCC_HOSTV_<NAME>__` mechanism in `src/host_shadow_macros.h`. `fenv_t` is host-sized (`__CCCC_SIZEOF_FENV_T__`), and `FE_DFL_ENV`, an opaque host address, is re-emitted as the bare identifier by `serialize_expr.c`.

Check `-m` or the `-c=native` output when debugging this class: `-E` shows pre-serialization text, where a correct sentinel can look wrong.

## Dropped build bodies

`[[cccc::build]]`/`[[cccc::build_target]]` bodies are always dropped from native output: the build runtime's `__builtin_build_*` calls are host-side only (`cc_load_build_runtime`) and have no native lowering, so emitting them would fail to link. `[[cccc::test]]` bodies are emitted with the same `__builtin_assert_*` runtime `--testing=native` uses, which is inert without a harness.

## Shared runtimes

`wide_bitint`, `c23_math`, `c23_mem` and `c23_strtol` each have one `runtime.c` that is both compiled into the VM and emitted verbatim as the native shim, so both agree by construction. See the project `CLAUDE.md` for the layout rules.
