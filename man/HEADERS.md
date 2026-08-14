# Header Resolution

How CCCC finds the headers a `#include` refers to: CCCC's own bundled
copies of the standard headers, a project's own headers, and genuine SDK
headers under `--use-system-headers`.

## Quick start

```bash
# Standard headers resolve with zero configuration, from any directory:
cd /anywhere
/path/to/cccc program.c            # #include <stdbool.h>, <stdio.h>, etc. just work

# A project's own headers still need the usual -I:
cccc -I./myproject/include program.c

# Prefer real SDK headers for non-owned standard headers:
cccc --use-system-headers --sysroot "$(xcrun --show-sdk-path)" program.c
```

## Search order

For a standard header (one CCCC knows about — see "Owned vs. known" below),
`search_include_paths`/the `PP_INCLUDE` handler (`src/preprocess.c`) try, in
order:

1. **User `-I` paths.** An explicit `-I` always wins, including over CCCC's
   own bundled copy — this is what lets a developer point at an edited
   `include/*.h` without rebuilding (`tools/tests.py`'s runner always passes
   `-I./include` for exactly this reason).
2. **The embedded `src/std.c` table.** Every standard header's full text is
   compiled into the `cccc` binary itself, regenerated fresh on every build
   (`tools/regen_stdlib.sh`, from `tools/generate_stdlib.c`). This is tried
   with **no filesystem access at all** — no CWD dependency, no need for a
   `./include` directory to exist anywhere. This is what makes
   `#include <stdbool.h>` (and, via `<implicit-reflection.h>`'s own
   `#include <stdbool.h>`, every `[[cccc::comptime]]`-using file) resolve
   from any process CWD with zero flags.
3. **`./include` on disk**, CCCC's own bundled header directory, as a
   fallback. This only matters for a stage0 build (linked against
   `src/std_stub.c`, which embeds nothing — see `make bootstrap` in
   `BUILDING.md`) and as a safety net if a build's embedded table is stale.
4. **System include paths** (`-i`/`--isystem`, `--sysroot`), gated by the
   rules below.

A non-standard header (a project's own `"local.h"`, or a header CCCC has
never heard of) skips steps 2–3 entirely and only ever resolves via `-I`
paths or system include paths.

## Owned vs. known standard headers

Two different classifications drive the rules above:

- **Known** (`get_std_header(name) != NULL`): CCCC has an embedded copy of
  this header at all. Roughly every C standard header plus a handful of
  POSIX ones.
- **Owned** (`is_compiler_owned_header` in `src/preprocess.c`): `stdarg.h`,
  `setjmp.h`, `stdbool.h`, `stddef.h`, `stdint.h`, `inttypes.h`,
  `complex.h`, `stdatomic.h`, `stdckdint.h`. These are tightly coupled to
  the VM's ABI or type system — `va_list`/`jmp_buf` layouts, the built-in
  boolean/integer types, `__cccc_*` builtin lowering for `creal`/`ckd_add`/
  `atomic_load`/etc. A genuine SDK copy of any of these either doesn't
  match CCCC's ABI or silently miscodegens. **Owned headers always resolve
  to CCCC's own copy, never to a system header, regardless of any flag** —
  there is no valid substitute.

## Flags

- **`--use-system-headers`**: for *known but not owned* standard headers,
  prefer a genuine SDK copy (searched via `-i`/`--sysroot`'s
  `system_include_paths`) before falling back to CCCC's own copy. Owned
  headers are unaffected — they still always resolve to CCCC's copy.
- **`--sysroot <path>`**: adds `<path>/usr/include` and
  `<path>/usr/local/include` to the system search paths; implies
  `--use-system-headers`.
- **`--no-builtin-includes`** (requires `--use-system-headers`): for known,
  non-owned standard headers, do **not** fall back to CCCC's own copy (step
  2/3 above) if the SDK copy isn't found — fail instead of silently
  substituting. Owned headers are exempt from this too, for the same
  reason: there's no SDK substitute to prefer in the first place, so
  "don't fall back" would just mean "never resolve `stdbool.h`", which
  isn't useful to anyone.
- **`CCCC_NATIVE_CC`** and friends (see `cccc_find_native_cc`) select the
  compiler `-c=native` shells out to; unrelated to header search but
  documented here since the interaction below is easy to miss.

## `-c=native` and auto-captured includes

`-c=native` emits C and hands it to a real host compiler (`cc`/`clang`/`gcc`
by default). Two things follow from that:

- CCCC's own bundled include directory is **never** forwarded to the native
  compiler (`run_native_backend` in `src/main.c` only forwards user-supplied
  `-I`/`-i`). If it were, the native compiler would see CCCC's polyfill
  headers (`typedef void FILE;`, etc.) fighting with the real system ones.
- CCCC auto-captures the primary source file's own top-level `#include`
  directives and re-emits them verbatim into the generated C
  (`preprocess.c`'s auto-capture; the include has to stay — e.g. `printf`
  is an FFI extern with no serialized prototype for the native compiler to
  see otherwise). A **quoted** include (`#include "local.h"`) that CCCC
  itself resolved relative to the primary file's own directory would
  otherwise be unresolvable to the native compiler, since the generated
  `.c` lives in a temp directory — so `run_native_backend` also forwards
  `-I<the primary file's own directory>` automatically.
- Because the real header is re-emitted via auto-capture, CCCC does **not**
  also re-emit type definitions it collected from that same header
  (`TypeNameRecord.from_include` in `src/cccc.h`, used by
  `cc_serialize_program`'s `!generated_only` path in `src/serialize.c`) —
  otherwise `typedef void FILE;` (from CCCC's own `stdio.h` polyfill) would
  collide with the real system stdio.h's `struct __sFILE`, and likewise for
  a header-defined aggregate like `struct tm` from `<time.h>`. Types
  synthesized by comptime/reflection code (no header of their own to
  collide with) are exempt and always re-emitted. This filter is itself
  skipped under `--emit-only`, since that flag turns auto-capture off — the
  header isn't re-emitted there, so the type definitions are still needed.
- A function declared with no body anywhere in the program (e.g. `int
  abs(int x);` with no matching definition) is serialized as a prototype
  only when it was written in a command-line input file (or in a
  cccc-only-routed include, whose own `#include` is never re-emitted — see
  below). The VM path needs no such prototype, since it resolves the call
  as an FFI symbol with a known signature; the native path hands the C to
  a system compiler, which needs an explicit declaration for any call. A
  header-sourced bare declaration is left out, since the auto-captured
  `#include` already supplies it to the native compiler. This check
  (`file_is_command_line_input()`, `src/serialize.c`) used to compare
  against `vm->compiler.primary_file` alone — pinned to the *first* input
  file forever — so the identical shape written in a *second or later*
  input file was misidentified as header-supplied and silently dropped;
  fixed by matching against every command-line input path, found while
  investigating #1002.
- A typedef/struct/enum written at file scope *in* a non-primary
  command-line input file, and that same input file's own top-level
  `#include` directives, are both serialized/replayed exactly like the
  primary file's — auto-capture and `TypeNameRecord.from_include` (above)
  used to key off `vm->compiler.primary_file` specifically (pinned to the
  *first* input file forever, same root cause as the previous bullet), so a
  type or `#include` written in a second-or-later input file was
  misclassified as "supplied by something else" and silently dropped from
  `-c=native`/`-m` output — `unknown type name`, then implicit-declaration
  errors for anything that file's own dropped `#include`s would have
  declared (#1006). Both are now keyed by
  `cc_file_is_command_line_input()`, matching the previous bullet.
  `run_native_backend` (`src/main.c`) forwards one `-I<dirname>` per
  command-line input file, not just the primary one, so a non-primary
  file's re-emitted quoted `#include "local.h"` still resolves. One
  residual: replayed directives from more than one input file can now
  collide in the same output (e.g. two files each `#define`-ing the same
  macro to different values) — harmless, since every input file has
  already been fully parsed into its own AST by the time this text is
  replayed (each with its own, per-TU preprocessor state, #1001); the
  replayed text exists only to bring types/library declarations into scope
  for the host compiler, so a colliding `#define` is at worst a host
  redefinition warning, never a semantic change. An identical `typedef
  enum { ... } Thing;` (or other scalar/enum typedef) written in more than
  one input file is legal, duplicate-definition C from C11 on (the default
  standard is C23) and is left as two identical typedefs in the output;
  `same_type_or_origin` (`src/serialize.c`) gained a structural comparison
  arm for `TY_ENUM` (mirroring the existing `TY_STRUCT`/`TY_UNION` one) so
  two *structurally identical* enums declared in different input files
  collapse to a single emitted definition instead of a hard "redefinition
  of enumerator" error — enum was the one aggregate kind #999's opaque-
  handle-preserving `copy_type()` exclusion doesn't apply to, since an enum
  has no forward-declare-then-complete idiom to protect.
- Two different `.c` inputs each independently defining `static int
  helper(void)` with no shared header compile and run correctly (each has
  its own `Obj`; internal linkage is respected — `cc_link_progs`
  deliberately never canonicalizes `static` symbols across TUs, #957), but
  `-c=native` used to merge both into one output file with two colliding
  definitions of the same identifier (#1002). `rename_colliding_static_names()`
  (`src/serialize.c`) renames every same-named `static` Obj but the first,
  among Objs declared in more than one distinct file — a name with no
  collision is left exactly as written.
- A header whose CCCC copy is the only implementation likely to exist on a
  typical host at all — `stdbit.h`, `stdckdint.h`, `threads.h`, `uchar.h`,
  `Availability.h`, `decimal_math.h` — is a different problem from the
  "CCCC's polyfill fights the real one" case just above: there, replaying
  the `#include` is *wrong* because the host has its own copy that would
  collide; here, replaying it is wrong because the host likely has **no**
  copy at all, and CCCC's own bundled include directory is never forwarded
  (previous bullet), so a downstream compile failed with `file not found`
  even though CCCC itself ran the program fine (#1003).
  `is_cccc_supplied_only_header()` (`src/preprocess.c`, next to but
  distinct from `is_compiler_owned_header()` — neither necessary nor
  sufficient for this: `stdckdint.h` is owned and `stdbit.h` is not) marks
  such a header cccc-only the moment `PP_INCLUDE` resolves it, reusing the
  #896 cccc-only machinery: the `#include` is suppressed and the header's
  own content is re-derived into the output instead, the same treatment a
  `@comptime`-routed include already gets. `decimal_math.h` is the one
  exception — its `static inline` wrappers bottom out in `extern
  __cccc_dec_*` symbols that exist only inside the VM's FFI runtime, so
  re-deriving would only trade one unresolvable reference for another; it
  is a hard compile error instead.
- CCCC's own polyfill headers (`stdio.h`, `errno.h`, `getopt.h` in
  `src/std.c`) define a handful of identifiers (`stdout`/`stderr`/`stdin`,
  `errno`, `optarg`/`optind`/`opterr`/`optopt`) as macros that expand, at
  preprocessing time, to a call into an internal accessor shim (e.g.
  `__cccc_stdout()`) so they reflect the real host state instead of being
  inert guest globals. Since that expansion happens before this backend
  ever runs, the AST already contains the shim call with no record of the
  original identifier. `cc_serialize_program` (`src/serialize.c`) defines
  each shim actually used (`serialize_native_accessor_shims`) in terms of
  the real symbol immediately after the real header is re-emitted — e.g.
  `static FILE *__cccc_stdout(void) { return stdout; }` — rather than
  leaving the call to an undeclared function in the generated C.

`-c=generated` (without `-c=native`) is a separate code path: its output is
meant to be compiled *alongside* normal headers, so it has never re-emitted
header-sourced typedefs (see `generated_only` in `serialize.c`). It has its
own, narrower version of the same collision problem: a struct/enum reached
through a `GetType()`/`Quote()` comptime macro is normally re-derived into
the output (a `#include @comptime`-routed header's own `#include` is never
replayed, so nothing else supplies the definition), but if the *same*
header is also plainly `#include`d in the same TU, the plain include's
`#include` line **is** replayed, and the definition must not also be
re-derived on top of it. `path_is_captured()` in `src/serialize.c` (#953)
resolves this by checking whether the type's declaring file is one of the
paths auto-capture actually replayed for this program — including a
standard header served from CCCC's embedded `src/std.c` table rather than
resolved on disk, which is keyed under its own synthetic
`<embedded>/<name>` path (`embedded_header_key()` in `src/preprocess.c`,
registered into the same map an on-disk resolution uses) rather than a real
filesystem path (#998).

`--emit-cccc` inverts the header-collision behaviour described above for
cccc-only includes: instead of skipping their re-emission (`cc_file_is_cccc_only`
in `preprocess.c`), the flag re-emits them verbatim, on the assumption that
whatever consumes the output (another CCCC instance, a future dialect-aware
tool) understands the routing syntax those files carry. Combined with
`-c=native`, this also disables the plain `cc`/`clang`/`gcc` PATH search
described above -- `CCCC_NATIVE_CC` must name a compiler explicitly, since a
plain system compiler cannot parse `[[cccc::...]]` dialect syntax. See
[COVERAGE.md](COVERAGE.md#attributes) for the full contract.

### Pointer arithmetic and global initializer reconstruction

The serializer reconstructs C source text from the AST, not from the
original source — pointer arithmetic and global initializers are printed
from a lower-level, already-scaled representation, so both need dedicated
handling to come back out as valid, semantically faithful C:

- **Pointer arithmetic.** By the time `p + i`/`p - i`/`q - p` reach the
  serializer, the offset has already been scaled to bytes and both operands
  cast to a common pointer type (`usual_arith_conv` in `src/type.c`) — so
  the naive `lhs OP rhs` printing that works for every other binary
  operator produces either invalid C (casting the byte offset to a pointer
  type: `ptr + (int *)offset`) or, if that cast were simply dropped, C that
  compiles but is silently 4×/8× wrong (the host would re-scale an
  already-scaled offset). `serialize_expr`'s `ND_ADD`/`ND_SUB` case
  recovers the original shape by peeling back to the pre-conversion operand
  (`strip_casts`) and re-casting through `(char *)` instead, so the offset
  is applied exactly once: `(T *)((char *)ptr + offset)`, and
  `((char *)q - (char *)p)` for pointer subtraction.
- **Global initializers.** `serialize_init_bytes` (`src/serialize.c`)
  reconstructs a global's initializer from its raw `init_data` bytes,
  recursing through arrays/vectors/structs/unions. A pointer-typed
  initializer slot backed by a `Relocation` (address of another global, a
  string literal, a function) resolves the real symbol reference instead of
  printing the placeholder-zeroed bytes. A union initializes through its
  **largest** member (recursing if that member is itself an aggregate) —
  byte-exact whenever some member spans the union's full (alignment-padded)
  size, which is the normal case.
- **Unsupported shapes fail loudly.** A union with no member spanning its
  full size (alignment padding can make the largest-*by-size* member still
  fall short — e.g. `union { char c[5]; int x; }` pads to 8 bytes but no
  member is 8 bytes wide) and `_Complex` global initializers have no
  verified byte-exact reconstruction and are refused with a `cannot
  serialize initializer` diagnostic naming the variable, rather than
  emitting a guess that would silently change the program's data. There is
  no equivalent gap for non-global (local/stack) initializers — those are
  codegen'd directly, not reconstructed as source text.
- **Anonymous globals.** `new_anon_gvar` (`src/parse.c`) hands out the same
  synthesized `.L..N` name to string literals, static locals, and compound
  literals (`(int[]){1,2,3}`, `&(struct S){...}`) alike — a dot isn't a
  valid C identifier character. `rename_anon_globals` (`src/serialize.c`)
  runs once before any other emission pass: every such Obj that isn't a
  genuine string literal (`Obj.is_string_literal`, set only by
  `new_string_literal`) is given a real, `static`-qualified name and
  definition, same as any other file-scope global. String literals
  themselves are still inlined at their point of use, never given their own
  definition.
- **Per-function local name collisions.** `serialize_function` hoists every
  entry on a function's `->locals` to one flat declaration list at the top
  of the serialized body. Since C source scoping lets sibling or nested
  blocks reuse a name (two `for (int i = ...)` loops in the same function,
  or an inner declaration shadowing an outer one — including a parameter's
  own name), the hoisting loop renames a *non-parameter* local on collision
  against every other local/param in the function, keeping the flattened
  declaration list free of duplicate identifiers. Parameters themselves are
  never renamed: `serialize_function_signature` has already printed the
  function's signature by the time this runs, so renaming a param's `Obj`
  this late would desync the signature from the body.
- **`for`-loop declaration-form init.** A `for (int i = 0; ...)` init
  clause parses as an `ND_BLOCK` of per-declarator `ND_EXPR_STMT` nodes
  (`declaration()`, `src/parse.c`), not a plain expression — the `ND_FOR`
  case in `serialize_stmt` emits each declarator's initializing assignment,
  comma-joined for a multi-declarator init (`for (int i = 0, j = 1; ...)`),
  and nothing for a declaration with no initializer (`for (int i; ...)`).
  The declarations themselves are already hoisted by `serialize_function`.

### Included files that use cccc-only routing

A file reached via a plain `#include` can itself use CCCC-only
preprocessor routing (`#include @comptime <...>`, `@shared`, `[[cccc::...]]`
spellings — see [Include scoping](MACROS.md#include-scoping) in
man/MACROS.md) — none of which means anything to a real system compiler.
If the primary file's auto-captured `#include` of such a file were
re-emitted verbatim, the native compiler would open it directly and choke
(`expected "FILENAME" or <FILENAME>` for `#include @comptime`, for example).

CCCC tracks which files use this routing — directly, or transitively
through their own plain `#include`s — and drops the auto-captured
`#include` line for any of them rather than handing broken syntax to the
downstream compiler. The file's own declarations are still serialized
normally (not treated as "already supplied by a re-emitted `#include`", the
way an ordinary header's are), so types and functions it defines remain
available in the generated native C. Passing such files as separate
positional arguments to `cccc` (`cccc -c=native -o out lib.c main.c`, no
`#include` at all) also works and avoids the situation entirely, since each
file is then preprocessed and merged by CCCC itself before native
serialization.

## Private headers

`include/cccc/reflection.h`, `testing.h`, and `building.h` are CCCC's own
internal headers, injected implicitly for `[[cccc::comptime]]`-using files,
`[[cccc::test]]` test binaries, and `[[cccc::build]]` build scripts,
respectively (`tokenize_private_header` in `src/preprocess.c`). They are
embedded under their bare names (`"reflection.h"`, etc.) purely so that
internal lookup function can find them — an ordinary
`#include <reflection.h>` in user code deliberately does **not** resolve to
them; the public spelling, if you ever need it directly, is
`#include <cccc/reflection.h>`.
