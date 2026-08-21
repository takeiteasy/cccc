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
- A captured conditional-group directive (`#if`/`#ifdef`/`#ifndef`/`#elif`/
  `#else`/`#endif`) is captured verbatim like any other top-level directive,
  but is **not** replayed into `-m`/`-c=native`/`-c=generated` output — it
  would always be an empty shell there anyway, since CCCC's own preprocessor
  has already resolved which branch was taken and captured only that
  branch's own content as its own separate lines/directives. Replaying the
  shell would hand the *evaluation* to the host compiler a second time, for
  no benefit and two real hazards: a host lacking a feature-test macro CCCC
  already resolved on its own (e.g. `#if __has_embed(...)`, which a real
  compiler predating that C23 feature rejects outright even though CCCC
  evaluated it fine), and a captured `#ifdef __CCCC__` shell being silently
  false at the host (which never defines that macro), dropping whatever a
  taken branch inside it captured (#1064). `--emit-cccc` is exempted, like
  the loop's other filters — dialect-fidelity output expects a cccc-aware
  reader.
- Every captured `#include` is replayed as one block, unconditionally, at
  the very top of the emitted C — ahead of every prototype and definition
  — regardless of where it actually appeared in the source. This can
  invert a legal declaration order: `tests/test_minilua.c` (#1042) defines
  `static int getmode(...)` and only *later* writes `#include <unistd.h>`
  — legal C (a later, weaker declaration of an already-defined `static`
  doesn't redefine it; `clang -fsyntax-only` on the real source confirms
  this) — but the hoisted include now sees macOS libc's real `mode_t
  getmode(const void *, mode_t)` *before* the static definition, "static
  declaration of 'getmode' follows non-static declaration", a collision
  that isn't in the user's own program. Fixed defensively rather than by
  tracking per-directive source position: `rename_colliding_static_names()`
  (`src/serialize.c`, #1002) now also probes the host libc's own symbol
  namespace (`dlsym` on the same handle `cc_load_libc()`/`find_libc()`
  already use for the VM's own FFI path — deliberately never
  `RTLD_DEFAULT`/`dlopen(NULL)`, which would also see the compiler process
  itself and make output depend on which `cccc` binary happened to run it)
  and renames any `static` whose name resolves there, the same
  `"%s__cccc_dupN"` rename already used for an ordinary cross-TU
  collision. A known, deliberate over-approximation: `dlsym` proves a
  *definition* exists in the host's symbol namespace, not that a replayed
  header actually *declares* it — harmless, since renaming a `static` never
  changes observable behavior (it's file-local, and every reference
  resolves through the same `Obj`).
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
  A cccc-only header's own **nested** `#include`s need separate handling
  from the re-derivation above (found closing #1022): re-derivation only
  covers types the parser itself saw declarations for, not a header pulled
  in transitively, so `include/threads.h`'s own `#include "pthread.h"`/
  `"time.h"` were neither replayed (the auto-capture gate only fired for a
  command-line input file) nor re-derived — a real host compiler
  reprocessing the re-derived text hit "unknown type name 'pthread_key_t'"/
  "'struct timespec' will not be visible outside of this function", types
  nothing declared. `mark_cccc_only_file` for the outer header runs before
  its own body is walked, so `cc_file_is_cccc_only` on the *including*
  file is already true by the time its own nested `#include` lines are
  processed — the auto-capture gate now also fires for those, not just
  command-line input files, so a plain (non-cccc-only) nested header gets
  replayed like any other captured include. The existing
  `cc_file_is_cccc_only` suppression in `serialize.c`'s own replay loop is
  unchanged, so a cccc-only header nested inside another cccc-only header
  is still correctly suppressed rather than replayed.
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
- A reflection-API comptime builder can resolve a call to a handful of
  well-known libc functions (`memcpy`/`memmove`/`memcmp`/`strlen`/`strcmp`,
  via `Serialize()`/`Deserialize()` or the `Memcpy()`/`Strlen()`/`Strcmp()`
  reflection.h macros directly) with no `#include` of the declaring header
  ever reaching -c=native output — either `ensure_libc_fn_decl()`
  (`src/reflection.c`) synthesizes a fresh `Obj` with no token/file at all
  because nothing else has declared the name yet, or the call resolves to a
  genuine `Obj` that reflection.h's own internal `#include <string.h>`
  leaves in scope (parsed for real by every comptime program, not gated on
  custom-attribute usage) — never a captured user `#include` either way, so
  auto-capture has nothing to replay. `register_synth_libc_call()`
  (`src/reflection.c`), reached centrally from `var_ref_lookup()` so both
  shapes are covered uniformly, records `{Obj, header}` into
  `vm->compiler.synth_libc_decls`; `serialize_synth_libc_includes()`
  (`src/serialize.c`) emits the real header once for whichever entries a
  program's emitted functions actually call (`node_calls_obj()`, an
  identity match), rather than a prototype — the synthesized signatures are
  deliberately loose and a printed prototype could conflict with the real
  declaration if `<string.h>` is also reached some other way in the same TU
  (#1050).
- The type-name sibling of the above: a comptime builder can fold a
  standard *scalar typedef name* — `GetType("size_t")`/`"ptrdiff_t"`/
  `"wchar_t"` — into a generated function's signature or body via
  `cc_comptime_resolve_type_name()`'s demand-driven splice (`src/macros.c`),
  which re-parses the typedef out of CCCC's own bundled `include/stddef.h`
  with no `#include` of it ever appearing in the TU. `record_type_name()`
  therefore marks the record `from_include=true`, so `typedef_alias_header_
  suppressed()` correctly drops its alias line under the ordinary
  assumption that a user `#include` supplies it — except nothing here ever
  does, leaving a bare, undeclared name. `serialize_synth_typedef_includes()`
  (`src/serialize.c`) emits the real `<stddef.h>` on demand, mirroring
  `serialize_synth_libc_includes()`'s shape: a small `{name, header}` table
  scoped to exactly the trio verified to match the real host's own typedef
  on every supported combo (LP64 macOS/Linux × aarch64/x86_64), plus a
  usage walk (`obj_needs_synth_typedef_header()`) rather than a printed
  typedef, for the same collision-avoidance reason. A program that already
  declares its own top-level typedef of one of these names is deferred to
  instead (`has_colliding_user_typedef()`) — forcing the header in on top of
  it can turn a harmless redundant redeclaration into a hard "typedef
  redefinition with different types" (#1057).
- `setjmp.h` is one of the few owned headers (`is_compiler_owned_header`)
  that also declares real functions, not just VM-only types — but relying
  on its auto-captured `#include <setjmp.h>` line to resolve to the real
  host header at native-compile time turned out to be fragile: a user `-I`
  path that happens to also contain CCCC's own bundled headers (this
  repo's own test harness's `-I./include` is exactly that) shadows the real
  header with CCCC's declaration-free copy — "call to undeclared library
  function". `cc_serialize_program` never replays the captured `#include
  <setjmp.h>` line at all, and instead always lowers `setjmp`/`longjmp`/
  `_setjmp`/`_longjmp` to calls to exactly `_setjmp`/`_longjmp` — plain
  `extern`-declared functions on every supported host, unlike `setjmp`
  itself (a macro on glibc, `#define setjmp(env) _setjmp(env)`) —
  declaring them itself (`serialize_synth_setjmp_decls()`, `void *`
  parameters, needing no `jmp_buf` type at all) whenever any of the four
  are actually called. `jmp_buf` (`include/setjmp.h`) is `long long[40]`
  (320 bytes), sized to cover every supported host's own real `jmp_buf`
  too, so a guest-folded `sizeof(jmp_buf)`/`offsetof` stays correct even
  though native storage is always CCCC's own structural type, never the
  host's `jmp_buf` alias (#1054/#1030).
- `stdarg.h` is the same owned-header shape as `setjmp.h` above. `struct
  va_list` (`include/stdarg.h`) is padded to 64 bytes (a trailing `char
  __reserved[40]`) so a guest-folded `sizeof(va_list)`/`offsetof` stays
  correct against every supported host's own real, larger `va_list`
  (measured: macOS arm64 8B, macOS x86_64 24B, glibc x86_64/aarch64 32B)
  — same reasoning as `jmp_buf` (#1059). Unlike `jmp_buf`, though,
  `va_start`/`va_arg`/`va_copy`/`va_end` are genuine macros usable in
  arbitrary expression/statement position, not a pair of plain function
  calls -- so the translation approach differs: rather than lowering to a
  fixed pair of `extern`-declared functions the way `setjmp`/`longjmp`
  lower to `_setjmp`/`_longjmp`, each macro in `include/stdarg.h` now
  wraps its existing (unchanged) VM-ABI expansion as the trailing argument
  to a new internal `__cccc_va_start`/`_arg`/`_copy`/`_end` builtin
  (`src/parse_postfix.c`). That builtin parses `ap`/`last`/`type`/`src` a
  *second*, independent time, purely to stamp them as serializer
  annotation (`Node.va_form` plus `va_ap`/`va_last`/`va_src`/`va_type`,
  `src/cccc.h`) onto the returned impl node -- the impl node itself,
  parsed and returned completely unchanged, is still the only thing VM
  codegen/comptime/reflection/inlining ever walk (`clone_expr`/
  `clone_subst`/`quote_substitute`/`transform_node` were all extended to
  clone/substitute/transform the three new `Node*` annotation fields too,
  so a macro or comptime clone of an annotated node can't silently
  degrade back to unannotated VM-ABI output). `serialize_expr`
  (`src/serialize.c`) checks the annotation before dispatching on
  `node->kind` at all, and when present prints the real host
  `va_start(ap, last)`/`va_arg(ap, type)`/`va_copy(dest, src)`/`va_end(ap)`
  form instead of walking the VM-internal subtree. No on-demand `#include`
  machinery was needed the way #1050/#1057 needed one for a
  reflection-API-resolved libc call: these four only exist as macros, so
  reaching one at all already requires the user's own
  `#include <stdarg.h>`/`"stdarg.h"` in the TU, auto-captured and replayed
  like any ordinary header, resolving via `include/stdarg.h`'s own
  `#include_next` hand-off (#1018).

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

This tracking (`mark_cccc_only_file()`/`cc_file_is_cccc_only()`,
`src/preprocess.c`) also covers a `[[cccc::comptime]]`/`__attribute__
((comptime))`-attributed declaration, not just a routed directive — a
header reached only through a plain `#include`, with no `@comptime`-routed
directive anywhere in it, but declaring its own comptime function or
variable, is marked the moment that declaration is recognized
(`try_extract_attr_macro`). Its body can reference reflection-API
constructs (`Obj`/`MakeFunction`/`GetType`/...) with no meaning to a host
compiler, so replaying it verbatim would fail the same way a routed
directive's file would — the unknown `[[cccc::comptime]]` attribute itself
is only ever a harmlessly-ignored warning on a real compiler, but the
comptime body text past it is not valid C at all (#1048). This marking
deliberately excludes `tokenize_private_header()`'s own synthetic tags
(`<implicit-reflection.h>`/`<building.h>`/`<testing.h>`, used to inject
CCCC's own reflection/testing/build headers) and `__builtin_quote`'s
`<quote>` pseudo-file by exact match — neither is ever reached through the
ordinary `#include` auto-capture path in the first place, but marking them
would still wrongly flip an internal reflection-API type's `from_include`
status elsewhere (`record_type_name()`, `src/parse_core.c`), the same
#1034/#892 regression a broader `"<...>"` prefix match hit before.

### Passing `-I` at CCCC's own bundled headers

The first bullet above notes that CCCC's own bundled include directory
(`vm->compiler.builtin_include_dir`) is never forwarded to the native
compiler automatically. But nothing stops a caller from passing that same
directory explicitly with `-I` — the test harness (`tools/testing/native.py`)
does exactly this, since `./include` at the repo root *is* CCCC's own header
source tree, and `run_native_backend` forwards every `-I` it's given
straight through to the host compiler (same as any other user-supplied
path). When that happens, a replayed `#include <foo.h>` in the generated TU
resolves back to CCCC's own copy of `foo.h` instead of the host's — fine for
most headers, which are self-contained, but wrong for the handful that need
macros only CCCC's own preprocessor injects (`fenv.h`'s
`__CCCC_SIZEOF_FENV_T__`/`__CCCC_FE_*`, `errno.h`'s `__CCCC_E*__` — see
`init_fenv_macros()`/`init_errno_macros()` in `src/preprocess.c`): a real
host compiler reprocessing that text from scratch has never heard of them
and fails outright (#1021).

`include/fenv.h`, `include/errno.h`, `include/stdio.h`, `include/getopt.h`,
`include/stdint.h`, `include/Availability.h`, and `include/sys/cdefs.h`
handle this by guarding their whole CCCC-flavored body behind `#ifdef
__CCCC__` (a macro CCCC's own preprocessor always defines before parsing
any header, guest-side) and `#include_next`ing the host's own,
self-contained header in the `#else` branch — taken only when a genuine,
non-CCCC compiler reprocesses this exact physical file, which only happens
during `-c=native`/`-c=generated` serializer replay with `-I` pointed at
this directory.

`Availability.h`/`sys/cdefs.h` are a distinct hazard within the same
guard-shape family, worth calling out because the failure mode is silent
rather than a compile error (#1083): `Availability.h`'s `#define
__attribute__(x)` (empty — needed so a real macOS SDK header's
`__attribute__((availability(...)))` calls don't choke CCCC's own tokenizer)
is fine under CCCC's own preprocessing, since CCCC parses `__attribute__` as
a builtin construct rather than expanding it as a macro. But a *real*
preprocessor keeps an object/function-like macro definition live for the
rest of the translation unit once it's seen — so once a real host header
chain (`<stdio.h>` → `sys/cdefs.h` → this file, the common case) pulls this
stub in, every later `__attribute__(...)` in the *user's own* source
silently reduces to nothing, no error, no warning — including attributes
`serialize.c` itself emits (constructor/destructor, #1020). Guarding the
whole stub body on `__CCCC__` and handing off to the real host
`Availability.h` (behind `#ifdef __has_include_next` / `#if
__has_include_next(<Availability.h>)`, nested rather than a single `&&`
condition — GCC has historically choked on the combined form, the same
macOS-clang-passes/Linux-GCC-fails shape this batch keeps relearning, #1020/
#1070/#1071) avoids the leak entirely; the `__has_include_next` guard keeps
the hand-off inert on a host with no real `Availability.h` at all (e.g.
Linux), where the branch is simply empty, matching the pre-fix shape for
such a host. `sys/cdefs.h`'s own `#include <Availability.h>` line is guarded
the same way, since that's the actual implicit pull-in path from real SDK
headers. `#include_next` works correctly here because the
file *was* reached via a real filesystem `#include` search (the `-I` that
found it), so continuing the search *after* that directory lands on the
real system header. Pick this treatment whenever there's no portable
`__builtin_*` (or literal constant) that can stand in for the real thing —
`stdout`/`stderr`/`optarg`/`errno`/`fenv_t` all bottom out in a
platform-specific real symbol or type with no such substitute, so the shim
genuinely needs the host's own header in scope (#1040: guarding only the
`extern` shim declarations, with no `#include_next`, still leaves the
`#define stdout __cccc_stdout()` macro live for the host compiler, so the
shim body `return stdout;` expands right back into a call to itself). Watch
for transitive fallout once `#include_next` is added to a *new* header:
`stdio.h`'s real host header transitively defines the actual fixed-width
integer typedefs (e.g. real macOS `int64_t` is `long long`, not this
codebase's guest `long`), which collided with `include/stdint.h`'s own,
until-then-unconditional `typedef long int64_t;` the moment both got pulled
into the same translation unit (`test_ffi.c`, `test_ffi_variadic_fnptr.c`)
— caught by the full native suite, not by `tools/comptime_native_smoke.py`,
which is why both must be run before trusting a header-guard change like
this one.

**Audit (#1084): `Availability.h`/`sys/cdefs.h` were the only two bundled
headers with the #1083 leak shape.** The class is narrow: a bundled
(non-owned) header reachable by the real host compiler under `-c=native`'s
`-I./include` forwarding — either directly, or implicitly via another real
system header's own `#include` — that unconditionally `#define`s something
whose effect reaches past its own file into later, unrelated code. Checked
directly, not assumed:

- Every bundled header's own macro surface was grepped for the
  keyword-shadowing spellings a real preprocessor could silently strip from
  later code the way `__attribute__` was (`__restrict`, `__inline`,
  `__asm`, `__volatile`, `__extension__`, `__typeof`, `__const`, `__signed`,
  `__unaligned`, `__nonnull`, `__always_inline`, bare `asm`/`inline`/
  `restrict`) — only `Availability.h` defined any of them, and only the two
  already fixed for #1083.
- Every hand-off header's own `#ifdef __CCCC__` … `#else #include_next`
  body was checked for internal `#include`s that could fire *before* the
  real host compiler reaches the `#else` branch: only `include/stdio.h`
  has any (`<stdarg.h>`, `"stddef.h"`), and both are inside its own
  `#ifdef __CCCC__` region — never reached when a real compiler is doing
  the reading. The rest (`errno.h`, `fenv.h`, `stdarg.h`, `getopt.h`,
  `stdint.h`) have no internal includes at all in their CCCC-flavored body.
- `math.h`/`float.h` (partially guarded — a real accessor-shim `extern`
  under `#ifdef __CCCC__`, but their own macros like `HUGE_VAL`/`isgreater`/
  `FLT_ROUNDS` are unconditional) are not hand-off headers themselves (no
  `#include_next <math.h>`/`<float.h>` anywhere): under `-c=native`, a real
  host compiler reads CCCC's own `math.h`/`float.h` as the *complete*,
  self-contained implementation, the same content the VM parsed — there is
  no second, genuinely-different real header for anything to leak into.
  At the time of this audit, every other non-owned bundled header
  (`pthread.h`, `unistd.h`, `sys/socket.h`, `time.h`, `signal.h`,
  `stdlib.h`, `string.h`, `dirent.h`, `fcntl.h`, `netdb.h`, …) was the same
  shape: a complete polyfill, never handed off, so there was no real-vs-CCCC
  collision to leak across. `pthread.h` has since gained its own hand-off
  (#1022, see below) — the underlying ABI-layout class this audit's own
  "complete polyfill" reasoning doesn't cover is now a named, tracked
  follow-up (a bundled polyfill header whose *struct layout*, not a macro,
  silently diverges from the real host's once something reaches it — see
  #1022's own ticket comment for the pthread_mutex_t/pthread_cond_t
  instance this class was first found in).
- `assert.h` unconditionally `#include <stdio.h>` (angle-bracket, outside
  any guard) — confirmed harmless directly (`cc -I<repo>/include -E` on
  `#include <assert.h>` followed by an `__attribute__`): `stdio.h` resolves
  itself correctly either way (CCCC's own content under CCCC's own
  preprocessing, hand-off to the real header otherwise), so an unconditional
  include of an already-self-guarding header is not itself a hazard.
- One unrelated, pre-existing, and *loud* (not silent — out of this
  audit's own scope, noted for completeness) `-Wmacro-redefined` warning:
  `include/stddef.h`'s `NULL` (an "owned" header, deliberately never handed
  off, so CCCC's own `NULL` always wins for the VM) gets redefined again by
  the real host's own `sys/_types/_null.h` once a hand-off chain (e.g.
  `assert.h` → `stdlib.h` → real `stdlib.h` → …) reaches it under
  `-c=native`. Both definitions are value-compatible and CCCC does not
  forward `-Werror` to the host `cc` by default, so this doesn't fail a
  build — flagged here only in case a future change makes it worth
  silencing.

No further action taken — no live bug found beyond #1083 itself.

**A bundled header with an `#include_next` hand-off must only ever be
`<...>`-included from another bundled header, never `"..."`-quoted** (#1070).
`include/math.h` used to reach `stdint.h` via `#include "stdint.h"` — under
`-c=native`'s `-I./include`-forwarding shape, a real host compiler resolves a
*quoted* include by the "same directory as the including file" rule, and real
GCC (confirmed: 13.3.0) then resumes its own `#include_next` search from
position 0 of the `-I` list rather than from where that quoted include
actually resolved — looping back to `./include`'s own `stdint.h` a second
time instead of continuing on to the real system header. The include guard
silently no-ops that second pass, so nothing gets defined at all
(`intmax_t`/`uintmax_t`, used by `math.h`'s own C23 `fromfp`/`ufromfp`
declarations, end up undeclared). clang resolves the identical scenario
correctly, which is why this stayed invisible through every prior
verification pass in this batch (all clang, either macOS or the amd64
container). Angle-bracket resolution sidesteps GCC's ambiguity entirely and
is otherwise free for any header on `is_compiler_owned_header`'s list
(`stdint.h`/`stdarg.h` here) — `force_cccc` in `search_include_paths()`
(`src/preprocess.c`) already forces CCCC's own copy for those regardless of
spelling. A non-owned `#include_next` header (`errno.h`, reached this way
from `include/threads.h`) is the one case where the switch is *not*
behaviour-neutral: under `--use-system-headers`, an angle-bracket include now
prefers the host SDK first, same as it would for any other non-owned header
reached by `<...>` — verified against `tests/test_use_system_headers_*.c`.
`tools/header_resolution_smoke.py`'s case 7 statically audits every bundled
header for a stray quoted include of an `#include_next` header, since the bug
class can't reproduce at all on a clang-only host and a round-trip test would
pass vacuously there.

A narrower version of the same problem hits any bundled header that
declares an `extern` prototype for a name `serialize.c`'s
`native_accessor_shims` table (see below) also gives a `static` definition
to once it's used (`__cccc_errno_ptr`, `__cccc_isnan_f`/etc): the replayed
extern and the emitted static definition disagree on linkage, and the host
compiler rejects the redeclaration outright. Where the rest of the header
doesn't need the full `#include_next` treatment — because the shim body can
be written in terms of a portable `__builtin_*` intrinsic instead of the
macro name it's standing in for (`include/math.h`'s `__cccc_isnan_f`/etc,
`include/float.h`'s `__cccc_flt_rounds`), or in terms of the *host's own*
internal accessor spelled directly (`include/stdlib.h`'s `MB_CUR_MAX`
shim, #1069: `__ctype_get_mb_cur_max()` on glibc, the plain global
`__mb_cur_max` on macOS, verified against both hosts' real headers) —
guarding just that one redundant `extern` declaration behind `#ifdef
__CCCC__` is enough either way: the shim's own `static` definition, always
emitted ahead of any use, serves as its own prototype. **Any new bundled
header declaring a name that gets a `native_accessor_shims` entry needs
one of these two treatments**, or it silently reintroduces this bug class
the next time something exercises it under `-c=native`. This isn't
hypothetical: #1052 added four new `native_accessor_shims` entries
(`__cccc_issignaling_{f,d}`, `__cccc_iseqsig_{f,d}`) right alongside the
already-guarded `__cccc_isnan_f` block in `include/math.h`, but didn't
guard their own declarations — invisible until a real `-I./include` native
run hit it (#1063).

`MB_CUR_MAX`'s shim (#1069) is also a worked example of the full
`#include_next` hand-off treatment turning out to be the *wrong* choice,
worth recording since every other case in this file picked it: a first
attempt gave `include/stdlib.h` its own hand-off (matching
`stdio.h`/`errno.h`/`fenv.h`/`math.h`) so the shim body could read the real
host's `MB_CUR_MAX` by `#include`-ing `<stdlib.h>` a second time — but that
chased the real host's own `<stdlib.h>` deep enough to hit a *second,
unrelated* instance of the same `-I./include`-shadowing hazard `setjmp.h`
first documented (above): real macOS's own `<_stdlib.h>` pulls in
`<sys/time.h>`, and CCCC's own bundled (non-hand-off) copy of that header
unconditionally `#include`s CCCC's own top-level `time.h`, defining a
`clock_t` that then collides with the real host's `clock_t` once
`sys/types.h`'s own chain reaches it too ("typedef redefinition"). Widening
the hand-off to cover every header transitively reachable this way has no
clean stopping point — the shim spelling the host's internal accessor
directly avoids the whole chain, and `include/stdlib.h` itself stays a
plain, non-hand-off bundled header.

`tools/audit_ffi.py`'s guard-presence check (`GUEST_ONLY_DECL_GUARDS`)
whitelists `__CCCC__` as a condition that only means something to CCCC's own
guest-side preprocessing, the same way it already does for
`__STDC_IEC_60559_DFP__` — without that, wrapping a declaration in `#ifdef
__CCCC__` would falsely report as "declared conditionally but registered
unconditionally" against `src/stdlib/*.c`'s unconditional
`cc_register_cfunc` calls.

**`include/pthread.h`'s bundled `pthread_mutex_t`/`pthread_cond_t` were a
struct-layout divergence, not a macro-leak one — a distinct hazard class
from everything else in this file** (#1022). Under the VM, `pthread_mutex_t`
is genuinely just a `{void *__handle; long __state; int __type;}` handle:
the real host mutex is lazily heap-allocated on first lock
(`src/stdlib/pthread.c`), and the guest never sees its real layout. Before
`-c=native`'s `-I./include` forwarding could ever put a real host compiler
in front of this same file, that was harmless. Once it can, the exact same
`{void*,long,int}` struct gets fed to the *real* `pthread_mutex_init()` —
24 bytes handed to a function that (macOS arm64) writes 64, silent heap
corruption with no compile or link error, flaking `test_pthread_mutex.c`'s
exit code under repeated native runs. Fixed with the same
`#ifdef __CCCC__` … `#else #include_next <pthread.h>` shape as
`stdio.h`/`errno.h`/`fenv.h`: a real host compiler now reads the real
`pthread_mutex_t`/`pthread_cond_t`/`pthread_t`/etc. directly, so the VM's
own opaque-handle projection is never present when the host's own
`pthread_*` functions actually run.

Handing `pthread.h` off surfaced two further instances of hazards this file
already documents elsewhere, both worth recording as *this specific header's*
concrete cases:

- **`_STRUCT_TIMESPEC`, an `-I./include`-shadowing collision (the general
  class first documented in `setjmp.h`'s own paragraph above), fixed
  narrowly instead of by handing off the colliding header too.** Both real
  glibc (`bits/types/struct_timespec.h`) and real macOS
  (`sys/_types/_timespec.h`) guard their own `struct timespec` behind the
  identical macro name `_STRUCT_TIMESPEC` (glibc's own header comments "NB:
  Include guard matches what `<linux/time.h>` uses"). Once `pthread.h`
  hands off, the real host `<pthread.h>`'s own internal chain reaches this
  struct — but `-I./include` still resolves that `#include <time.h>` to
  CCCC's own bundled (non-hand-off) copy first, which defined `struct
  timespec` unconditionally: "redefinition of 'timespec'" once the real
  chain's own copy is reached moments later. A full hand-off for
  `include/time.h` itself was tried and reverted — exactly the "no clean
  stopping point" shape `MB_CUR_MAX`'s own paragraph above already warns
  about: it also drags in the real host's `clockid_t`, which collides with
  the plain `typedef int clockid_t;` its sibling `include/sys/types.h`
  supplies, with no narrow fix available for *that* collision. Instead,
  `include/time.h` stays a plain bundled header and now additionally
  `#define`s `_STRUCT_TIMESPEC` right after its own `struct timespec`
  definition — mimicking the real headers' own guard so that whichever
  chain reaches the type second (real or CCCC's) sees it as already
  provided and no-ops instead of redefining it.
- **`__clockid_t`, glibc's private name for the same type its own public
  `clockid_t` aliases.** Real glibc `<pthread.h>`'s clock-based extensions
  (`pthread_mutex_clocklock`/`pthread_cond_clockwait`) spell their
  parameter with the leading-underscore name, normally supplied by glibc's
  own `<bits/types.h>` — but `-I./include` shadows that transitively too,
  via CCCC's own bundled (non-hand-off) `include/sys/types.h`, which only
  ever defined the public name. Added `typedef int __clockid_t;` alongside
  the existing `clockid_t` typedef, Linux branch only (Apple's own
  `<pthread.h>` has no such glibc-only extension to reach).
- **`pthread_t`'s own layout still cosmetically diverges, deliberately left
  as-is.** CCCC's own `pthread_t` is `void *`; glibc's real one is
  `unsigned long`. Both are 8 bytes on every platform/arch this project
  targets, so a native `pthread_create(&t, ...)` call built against the
  guest's own `pthread_t`-typed variable and the real host's
  `pthread_t *__restrict` parameter is bit-for-bit correct either way — the
  host compiler emits an `-Wincompatible-pointer-types` warning, not an
  error (CCCC doesn't forward `-Werror` to the host `cc` by default, the
  same acceptance already recorded for `stddef.h`'s `NULL` redefinition
  warning above). Not pursued further: fixing the *typedef spelling* to
  match would need a host-conditional `pthread_t`, more machinery than a
  cosmetic, harmless warning justifies.

**`include/sys/mount.h`'s bundled `struct statfs` was the same struct-layout
divergence class as `pthread.h`'s above, and the ticket that closes it
(#1031) also generalizes the fix** — this is the invariant to check first
whenever a new bundled header's `#include_next` hand-off is being added.
Before: any guest-folded `sizeof`/`_Alignof` of a `from_include` type
(`serialize_type_defs_for_owner`'s own suppression of the type's *body*, in
favor of the replayed `#include`) stayed CCCC's own possibly-stale literal
even once the real host layout won at native-compile time —
`test_sys_mount_statfs.c`'s canary caught exactly this: `sizeof(struct
statfs)` folded to CCCC's ~56-byte projection, so a `malloc()`'d buffer was
undersized once the real host `statfs()` (~2100 bytes on macOS) wrote into
it. Fixed generally, not just for `struct statfs`: a `sizeof`/`_Alignof`
node now carries the operand `Type` it was folded from
(`Node.layout_ty`/`layout_is_align`, `src/cccc.h`, set at the four fold
sites in `src/parse_postfix.c`), and `serialize_expr`'s `ND_NUM` case
re-materializes the operator textually — `sizeof(struct statfs)` rather
than a bare `56ULL` — whenever `type_layout_is_host_owned()`
(`src/serialize.c`) says the type's own definition is from_include-
suppressed (recursing through an array base or a struct/union's members,
stopping at a pointer) **and** `type_has_printable_name()` confirms
`serialize_type()` can spell it by a real tag/typedef rather than falling
through to a re-derived anonymous body (which would reinstate CCCC's own
projection right where this is trying to avoid exactly that — falls back
to the folded literal in that case). `include/sys/mount.h` itself needed
the same `#ifdef __CCCC__` … `#else #include_next <sys/mount.h>` hand-off
`pthread.h` has, plus the same `-I./include`-shadowing chain repair: real
macOS `<sys/mount.h>` reaches `<sys/attr.h>` → `<bsm/audit.h>`, which need
`u_char`/`u_short`/`u_int`/`u_long`/`u_int{8,16,32,64}_t` (BSD legacy
aliases), `size_t`/`time_t`, and the `NGROUPS`/`MAXHOSTNAMELEN` macros —
none of which CCCC's own (non-hand-off) `include/sys/types.h` supplied,
since `-I./include` shadows the real `<sys/types.h>` for every `#include`
in the TU, not just this file's own (the same shape `setjmp.h`'s
`_STRUCT_TIMESPEC`/`__clockid_t` paragraph above documents). Added
narrowly to `include/sys/types.h`'s `__APPLE__` branch — a short, closed
set confirmed by compiling the real chain to exhaustion (`clang
-ferror-limit=0`) rather than assumed, unlike the `time.h` full hand-off
attempt above, this one did **not** keep growing.

**`stdarg.h`'s `va_list` and `setjmp.h`'s `jmp_buf` are deliberately
excluded from this re-materialization** — both use the *opposite* strategy
(widen CCCC's own layout to cover every supported host's real one, so the
guest-folded constant is already a safe, correct-by-construction upper
bound on purpose, see their own paragraphs above) — re-materializing the
operator for them would replace that safe padded literal with whatever the
real host's own (possibly smaller) size happens to be, defeating the
padding #1054/#1059 built specifically to avoid depending on that.
`type_layout_is_host_owned()` excludes any type whose owning header is on
`is_compiler_owned_header()`'s fixed list (now shared between
`src/preprocess.c` and `src/serialize.c` for exactly this reason) —
confirmed necessary the hard way: `tools/comptime_native_smoke.py`'s own
case 97 (`sizeof(va_list)` folds to exactly 64) regressed without it.

A folded layout constant reached through any context other than a bare
`sizeof`/`_Alignof` **expression** node has no node left by the time
serialization runs to re-materialize from by default — each such context
calls `const_expr()`/`eval()` and keeps only the resulting `int64_t`, so it
stays folded. #1095 closed three of those: an array dimension
(`char buf[sizeof(struct statfs)]`, local or uninitialized-global only — see
below), a `case` label, and an enum value, sharing `const_expr_layout()`/
`node_layout_const()` (`src/parse_analysis.c`) to carry the same
`Type`/`is_align` provenance #1031's own `Node.layout_ty`/`layout_is_align`
pair does, and `serialize_layout_const()` (`src/serialize.c`, factored out
of #1031's own `ND_NUM` re-materialization arm) to emit it. An enum
constant's provenance also has to reach every *use* of the enumerator, not
just its own `NAME = ...` line in the body — `VarScope.enum_layout_ty`
carries it from the enum-specifier parse into `primary()`'s enum-constant
`ND_NUM` synthesis, the same way a `sizeof`/`_Alignof` expression's own node
would.

Two consistency hazards `#1095` had to guard against, both instances of the
same rule — "leave the inconsistent case folded rather than let two
serialized quantities disagree about the same value":

- **An array dimension on an *initialized* global** (`static char
  buf[sizeof(struct statfs)] = {...};`) stays folded: `serialize_init_bytes`
  sizes the byte image off the folded value, and re-materializing only the
  declared dimension would leave the two disagreeing (`SerializeContext.
  allow_layout_dims`, set only around a local's or an uninitialized global's
  own declarator). The same reasoning excludes an array dimension on a
  struct/union **member** — the enclosing aggregate's other member offsets
  are folded against it, matching the bitfield-width reasoning just below.
- **An enum constant that a later enumerator auto-increments from**
  (`enum { N = sizeof(struct statfs), M };`) also stays folded: `M`'s own
  value is `N`'s folded value plus one, computed once at parse time — if `N`
  alone re-materialized, `M == N + 1` could go false under `-c=native` (the
  real host's `sizeof` vs. the still-guest-folded `M`). Detected per
  enumerator during parsing (an enumerator with no `=` of its own clears the
  *previous* one's provenance, both on its `EnumConstant` and its
  `VarScope`) rather than assumed away.

**Still open, and not merely deferred — both are unsound to fix, not just
incomplete (#1099, closed `WONT_FIX` — record-keeping only, no code change
expected):**

- A **bitfield width** (`int x : sizeof(struct statfs);`) determines the
  *containing struct's own layout*, and CCCC emits that struct's body with
  member offsets it computed from the folded width. Re-materializing the
  width would make the host lay the aggregate out differently from every
  other folded offset CCCC already emitted for it — this is not a
  narrower version of the array-dimension fix above, it is actively wrong
  to attempt the same way.
- `serialize_init_bytes`'s own global-initializer byte image (the excluded
  initialized-global case above) has no independent fix within this
  scope — closing it would mean re-deriving the byte image itself against
  the real host layout, not just the declared dimension.

**`_Static_assert(sizeof(struct statfs) == N, "...")` was a different
problem from the two above, not a residual of this same fix, and is now
closed (#1098):** the serializer never emitted `_Static_assert` at all, so
there was no *divergence* in the emitted C to fix — the gap was that a host
whose real layout would fail the same check compiled anyway, silently. This
needed genuinely emitting the assert for the host to re-verify, a small
feature rather than a re-materialization fix. `static_assert_decl()`
(`src/parse_stmt.c`) now keeps the parsed condition `Node` (previously
discarded the moment `const_expr()` folded and checked it) instead of just
the folded `int64_t`, threading it through a new `Node.static_assert_cond`/
`static_assert_msg` pair for the block-scope form (stashed on the otherwise-
empty `ND_BLOCK` `stmt()` already returns) and a new `Compiler.
static_asserts` list for the file-scope form (which has no `Node` of its
own to hang it off). `serialize_static_assert()` (`src/serialize.c`)
re-emits `_Static_assert(cond, "msg")` — always the two-arg spelling, even
for a C23 single-arg `static_assert` source, since it needs no `<assert.h>`
and is valid on every host this project supports — through
`serialize_expr()` (so tag/typedef renames apply, unlike a raw-token
replay) and `serialize_string_n()` (so an escaped/embedded-quote message
round-trips). Gated on two conditions, both required:

- `expr_has_host_owned_layout()` walks the condition tree for a bare
  `sizeof`/`_Alignof`-of-a-`from_include`-type leaf (`node_layout_const()`)
  whose type is host-owned (`type_layout_is_host_owned()`) — deliberately
  narrower than that function itself, which accepts any `Type` kind despite
  its own doc comment saying "struct/union": a bare scalar type like plain
  `int` can spuriously `same_type_or_origin()`-match an unrelated
  `from_include` *typedef* of `int` (e.g. `sys/types.h`'s `__int32_t`,
  reached merely by including `<sys/mount.h>`) via that function's
  pointer-identity walk up the `origin` chain — harmless for #1031's own
  re-materialization (`sizeof(int)` prints identically either way) but
  would make this gate fire on ordinary, fully portable asserts having
  nothing to do with a host-divergent layout. Restricted to `TY_STRUCT`/
  `TY_UNION` (peeled through `TY_ARRAY`/`TY_VLA`) instead, matching every
  real-world case in this batch's own tickets.
- The assert's own token must be from a command-line input file (the same
  `cc_file_is_command_line_input()`/`cc_file_is_cccc_only()` test #901/
  #1096's bodiless-declaration gate uses) — load-bearing: `include/
  sys/stat.h`, `signal.h`, `fts.h`, `aio.h`, `mqueue.h`, and `ndbm.h` all
  carry their own per-platform `_Static_assert(sizeof(struct X) == N)` on
  types that *are* `from_include` and not compiler-owned; without this
  gate they would be re-emitted and fail against the real host layout for
  reasons the user never wrote.

Documented in `man/COVERAGE.md`'s Serialized-output divergences table.

**A bodiless declaration (`extern int close(int fd);`) sourced from one of
CCCC's own bundled headers, rather than the primary source file, is now
emitted too (#1096)** — found verifying #1031's own fix against
`tests/test_sys_mount_statfs.c`, and a real gap a plain
`cccc -c=native foo.c -o foo` user (no `-I./include`) could hit: bundled
`fcntl.h` itself `#include`s bundled `unistd.h`, which is what actually
declares `close()`, so the `#901` bodiless-declaration gate (`src/
serialize.c`) saw `close()`'s own token pointing at `unistd.h`, judged that
"not the primary file", and — on the assumption that whichever header
declared it must be the one the auto-captured `#include` replays — dropped
the prototype. That assumption holds for a **real host** header reached
transitively (the host compiler replaying the top-level `#include` walks
the identical chain, so the declaration genuinely is supplied) but not for
one of CCCC's own bundled headers, whose chain can differ from the host's:
the replayed `#include <fcntl.h>` resolves to the *host's* `fcntl.h` under
`-c=native`, and macOS's/glibc's own `fcntl.h` does not declare `close()`
("use of undeclared identifier 'close'"). The test suite's own
`tools/testing/native.py:87` always passes `-I./include`, which happens to
mask this (see below), so it shipped unnoticed.

Fixed via a new `Compiler.cccc_bundled_files` marker
(`cc_file_is_cccc_bundled()`/`mark_cccc_bundled_file()`, `src/
preprocess.c`) — the bundled-header analog of the existing `cccc_only_files`
marker (#896), registered at every place a header resolves to one of
CCCC's own (the embedded `src/std.c` table, an on-disk `-I./include` hit,
*and* a bundled header quote-including another bundled header by relative
path — `fcntl.h`'s own `#include "unistd.h"` takes a distinct early-return
branch in the `PP_INCLUDE` handler that none of the other two paths cover,
and missing it left the fix a no-op under the exact invocation shape this
ticket's own repro uses). The `#901` gate now also emits a bodiless
declaration when its own header is bundled-but-**not** itself replayed
(`path_is_captured()`, extended to populate outside `generated_only` mode
for exactly this) — gated on `obj->is_used` so an unrelated, unused
declaration from the same bundled header (`unistd.h` declares dozens of
functions besides `close()`) isn't also dumped into the output.

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
