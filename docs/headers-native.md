# Headers Under Native Output

Auto-captured includes and host-owned layouts when lowering to C.

## `-c=native` and auto-captured includes

Under `-c=native` (and `-m` / `-c=generated`), CCCC emits C and hands it to a
real host compiler, so header handling shifts:

- CCCC re-emits the source's own top-level `#include` lines verbatim into
  the generated C, and forwards the flags a host compiler needs to resolve
  them (`-I` for the source file's own directory, the real SDK search
  path). Under `-c=native` / `-m` this covers every command-line input,
  since the whole program is emitted. Under `-c=generated` only the
  **primary** input's directives are replayed: additional inputs there are
  comptime-support modules whose runtime code never reaches the output, so
  their `#include`/`#define` scaffolding would only be dead weight (and
  force an extra `-I` on the downstream `cc`). Route a directive with
  `@emit` or `@shared` to opt one in from such a file. Only a **top-level**
  `#include` is replayed this way: one written inside a function body is
  block-scoped in C, so it is never hoisted to file scope — its contents
  are parsed and serialized in place, inside the function that contains it,
  same as any other statement.
- Where a user `-I`/`-isystem` entry also contains CCCC's own bundled
  headers, it is demoted to `-idirafter` so the real host header always
  wins — CCCC's bundled copies are VM polyfills and must not shadow the
  real system headers in a native build.
- The `--std=` value is forwarded only as a flag-spelling probe; the
  emitted dialect is a fixed GNU C11 floor regardless (see
  [native.md](native.md)).
- A comptime-generated declaration's type must come from a header that is
  actually captured into the output — an `#include @shared`d header
  qualifies; one reached only via `#include @comptime`/`@build`/`@test`
  does not, and `-c=generated` reports a compile error rather than writing
  C nothing declares the type in. See
  [macros.md](macros-global.md#emit-directives-and-includes-in-generated-output).
- A header reached through a chain of captured, ordinary (non-bundled)
  `#include`s is treated as captured too, however many hops the chain
  runs — a user header's own `#include "helper.h"`, whose own
  `#include <nl_types.h>` in turn reaches a CCCC bundled header, is
  replayed as part of that chain's own text, so the host compiler reaches
  the real `nl_types.h` the same way it reaches every header that names
  it, and no bodiless declaration is needed anywhere along the chain. A
  bundled header reached through *another* bundled header (e.g. bundled
  `fcntl.h`'s own `#include "unistd.h"`) is the one case this does not
  cover: the replayed `#include <fcntl.h>` resolves to the *host's*
  `fcntl.h` under `-c=native`, whose own include graph CCCC cannot vouch
  for, so a bodiless declaration sourced from such a chain is still
  re-derived from CCCC's own header text rather than relied upon, the same
  as any other declaration this section describes. Two care points there:
  its declarator is parenthesized (`int (getc_unlocked)(FILE *stream);`),
  so a host header spelling the same name as a function-like macro (the
  unlocked-stdio family, or a `_FORTIFY_SOURCE` checked-function rewrite)
  doesn't expand it away; and a pointer parameter or return type that is
  itself a from_include typedef is spelled by that alias rather than
  decomposed to its underlying pointer type, so it still matches the real
  header's own declaration once both are visible to the host compiler.
- Any captured `#include` target that itself supplies a function's
  definition text -- a deliberate textual `.c` amalgamation (e.g.
  `#include "ops.c"`) or a vendored single-header **library** (a `.h` built
  via an `#define FOO_IMPLEMENTATION` idiom in exactly one TU) -- already
  supplies every function it defines to the output, external linkage
  included; none of them are re-serialized on top of the replayed
  `#include`. This holds even when the library's own public declarator
  names are macro-token-pasted (e.g. `#define DECORATE(name)
  prefix_##name`): the decision is keyed on where the function's body text
  is actually *written* (its macro-expansion site), not on the pasted
  token's own spelling location, which would otherwise misattribute it to
  wherever that macro happens to be defined. The one case this does NOT
  cover is a captured header's macro that only *produces* a function
  definition when invoked elsewhere -- a `#define DEFINE_THING(T) int
  thing_##T(void) { ... }` invoked in an ordinary command-line-input file --
  since the invocation site, not the header, is what actually needs to
  supply that body; the same expansion-site test correctly leaves this
  case alone. The same expansion-site rule governs a **bodiless
  declaration** and a **global** produced the same way: a captured header's
  macro that only *declares* a function, or that *defines* a global, when
  invoked elsewhere is likewise resolved by where it is invoked, not by
  where the producing macro happens to be written.
- A called `extern inline __attribute__((gnu_inline))` function from a
  captured user header also gets a weak out-of-line definition after the
  replayed `#include`, matching the VM: a real definition elsewhere wins,
  and copies from separately compiled `-c=object` files merge. A system
  header's copy is left to the host library.
- A function an input declares before a replayed `#include`, and that the
  header's own function bodies call, is prototyped ahead of that `#include`.
  This applies whether its definition is in another input or nowhere.
- When several TUs `#include` the *same* vendored single-header library,
  only one of the replayed lines survives — the rest are the ordinary
  `#include` dedup any repeated header goes through — but which one
  survives matters here, unlike for a plain header: only the TU that
  defines the `IMPLEMENTATION`-style macro *ahead of* its own `#include`
  actually supplies the function bodies, and the replayed output shares one
  accumulated macro namespace across every TU. The surviving line is placed
  at the position of the **last** TU's own `#include` whenever some other
  captured `#define`/`#undef` line sits between two TUs' occurrences of it —
  so every TU's own configuration macro is in effect once the header is
  replayed — and left at the first occurrence otherwise, which is every
  ordinary (non-configuring) repeated `#include`. One shape this does not
  cover: an *earlier* TU that captures a `#define`/`#undef` colliding with
  the header's own configuration macro *after* its own `#include` line
  would see that macro applied a second time, ahead of the header, when it
  wasn't meant to configure that inclusion at all — a pathological ordering
  no real vendored-header usage produces.

### Implementation-style headers that call back into your code

A header configured through macros can call your own functions or touch your
own statics from its bodies:

```c
static void my_log(const char *m) { /* ... */ }
#define LIB_LOG(s) my_log(s)
#include "lib_impl.h"      // bodies expand LIB_LOG(...)
```

The replayed output prints a prototype for each such function or static
immediately before the `#include` that brings that header in, so the host
compiler sees it when the header's bodies expand. The header can be the
`#include`d one or any header it includes.

The types a prototype names are declared there too:

| Signature names | Printed ahead of the `#include` |
| --- | --- |
| a pointer to a tagged `struct`/`union` | `struct P;` |
| a `struct`/`union`/`enum`, or a typedef of one, by value | its full definition |
| a typedef of your own (`typedef int my_t;`) | the typedef |
| a typedef an earlier replayed header supplies (`size_t`, `uint32_t`) | nothing |
| `size_t` and its siblings, reached only through another standard header | `#include <stddef.h>` |

A `static` whose signature names a type that cannot be declared yet is a cccc
error naming the function and the header: a type the replayed header itself
declares and no earlier `#include` supplies, a function-local type, or a
`_BitInt` wider than 128 bits. A non-static function with such a signature is
left to the host compiler's own diagnostic.

A captured `#define` whose trailing `/* ... */` comment spans several lines is
replayed on one line, comment text kept.

### Host-owned type/struct layout under self-hosting

When cccc compiles its own `src/stdlib/*.c` FFI-translation source as guest
input, a bundled header whose real host layout genuinely diverges from
CCCC's own guest-side projection (`sys/mount.h`'s `struct statfs`,
`sys/statvfs.h`'s `struct statvfs`, `fenv.h`'s `fenv_t`) needs its own
`#ifdef __CCCC__` / `#include_next` hand-off — the `-idirafter` demotion
above is a directory-wide backstop for a *user* `-I` entry, not the
mechanism that keeps a self-hosted `-c=native` build's own type layout
correct. Without the hand-off, the guest-side parse resolves the type
against CCCC's bundled projection, and that's what any `sizeof`/member
access folds against in the emitted C too.

This is why type-layout shadowing self-corrects far more often than macro
*value* shadowing does (see `src/host_shadow_macros.h`): a `sizeof(struct
statfs)` or `host_buf->f_bsize` re-materializes as literal source text in
the generated C, resolved by the real host compiler against the header the
hand-off replays — no injection step is needed once the hand-off exists,
unlike an `_SC_PAGESIZE`/`POLLWRBAND` numeric constant, which is baked into
the AST as an integer at cccc's own preprocess time and has nothing left to
resolve against once the guest value has already been substituted in.
`#1035` is the same shape one level up: `fenv_t` itself is already
host-sized (`__CCCC_SIZEOF_FENV_T__`, injected transitively the same way
errno codes and `CLOCK_*` ids are), and the one host-side-only *value* it
carries (`FE_DFL_ENV`, an opaque host address on some platforms) is
recognized and re-emitted as the bare identifier by a dedicated
`serialize_expr.c` case rather than folded to a number.

`-E` is the wrong probe for this class — it only shows the preprocessed
*text*, before serialization ever runs, so a still-correctly-substituted
sentinel can look wrong there. Check `-m` (or `-c=native`'s actual output)
instead.

See [native.md](native.md) for the native pipeline itself.

## URL includes under `-c=native`/`-m`/`-c=generated`

A URL `#include`/`#embed` (curl-enabled builds only — see
[install.md](install.md)) downloads only when `--allow-url` is passed; a URL
already in the cache resolves without it, and `__has_include`/`__has_embed`
never download without it. Downloads go into a shared on-disk cache
(`--url-cache-dir`, default `$TMPDIR/.cccc`) on every path, VM included. Every
fetch is also mirrored into that same directory under a URL-shaped path
(`<cache-dir>/https:/<host>/<path>`), so a raw `#include "https://…"` line
resolves exactly as written given that directory in the search path — at any
depth in the include graph, whether the directive sits in a command-line
input, one of CCCC's own bundled headers, or an ordinary project header that
itself `#include`s a URL.

Under `-c=native`, CCCC forwards the cache directory to the host compiler
itself (`-idirafter`), so nothing further is needed. A captured top-level URL
`#include` is additionally rewritten to an absolute `#include` of its
resolved cache entry, for tidier output; the mirror is what makes an
uncaptured one (reached only through a project header) resolve too. A URL
`#embed` never needs either treatment: like every captured `#embed`, its
bytes are already spliced into the token stream at parse time and the
directive itself is dropped from the replayed output.

Under `-m`/`-c=generated`, the dump is compiled by the caller, so CCCC has no
way to pass `-idirafter` on their behalf — when a URL was fetched, the output
carries a leading comment naming the exact flag to add
(`/* cccc: compile with -idirafter <cache-dir> (URL #include mirror) */`).

One consequence worth knowing: the resolved path (in the top-level rewrite,
and implicitly via the mirror) lives under the URL cache directory. If the
output is saved and compiled later (a different process, a different
machine, or after `--url-cache-clear`), the include only resolves as long as
the cache entry — flat or mirrored — is still there.

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

`include/cccc/kernel.h` is a public header, included as
`#include <cccc/kernel.h>`: it declares the work-item builtins and `cccc_launch`
described in [kernels.md](kernels.md). `include/cccc/opencl.h` maps OpenCL C
onto it and is included automatically before an OpenCL input
([opencl.md](opencl.md)). Both are embedded, so they resolve with no `-I`.
