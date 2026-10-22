# GNU Function and Allocation Attributes

Purity, optimization, sentinel, allocation and designated-init attributes.

## `__attribute__((pure))` / `[[gnu::pure]]`

Marks a function as *pure*: it may read global state but has no side effects
and always returns the same result for the same arguments given unchanged
global state.

**Effect:** parsed and stored on the function. The VM has no optimiser, so
this drives no dead-call elimination or CSE — it is informational (visible to
`__has_attribute` and reflection) and forwarded to the host cc under
`-c=native`, which acts on it.

Accepted spellings: `__attribute__((pure))`, `__attribute__((__pure__))`,
`[[gnu::pure]]`, `[[cccc::pure]]`.

**`__has_attribute`:** returns `1` for `pure` and `__pure__`.

---

## `__attribute__((const))` / `[[gnu::const]]`

Marks a function as *const*: it has no side effects and its return value
depends only on its arguments (no global reads).  Stronger than `pure`.

**Effect:** parsed and stored, informational only in VM mode (no optimiser);
forwarded to the host cc under `-c=native`.

Accepted spellings: `__attribute__((const))`, `__attribute__((__const__))`,
`[[gnu::const]]`, `[[cccc::const]]`.

> **Note:** `gnu::const` would collide with the C keyword `const` if written
> as `[[const]]` — the `gnu::` namespace qualifier is required for the C23
> spelling.

**`__has_attribute`:** returns `1` for `const` and `__const__`.

---

## `__attribute__((optimize(...)))` / `[[cccc::optimize(N)]]` / `@optimize(N)`

Parsed, syntax-checked, and then **ignored** with a `-Wignored-features`
warning. The VM has no optimiser, so a per-function optimisation level has
nothing to act on. The attribute is kept in the grammar for compatibility
with GCC/Clang sources that carry it; use `-c=native -O<n>` if a real host
optimisation level is wanted.

Accepted spellings: `__attribute__((optimize("O2")))` (string form `"O0"`
through `"O4"`, optional leading `-`), `[[cccc::optimize(3)]]` (integer `0`
through `4`), and the `@optimize(2)` shorthand. A malformed argument is still
a hard error.

**`__has_attribute` / `__has_c_attribute`:** both return `1` for `optimize`
(as a CCCC vendor attribute).

---


## `__attribute__((sentinel))` / `__attribute__((sentinel(N)))` / `[[gnu::sentinel]]`

Marks a variadic function as requiring a `NULL`-terminated argument list —
the classic example is `execl()`/`execlp()`. Bare `sentinel` requires the
*last* argument to be a literal `NULL`; `sentinel(N)` allows `N` trailing
non-sentinel arguments to follow the required `NULL` (i.e. the `NULL` must
be `N` positions back from the end of the call).

This is a **static, call-site-only** check — a syntactic scan of the parsed
argument list, with no runtime enforcement. Only a literal/constant-folded
*pointer-typed* null (`NULL`, `(void*)0`, `(T*)0`, C23 `nullptr`) satisfies
the check; a variable that merely holds `NULL` at runtime still warns, since
the parser has no flow analysis over the value (unlike `-Wmaybe-nonnull`'s
dataflow pass — sentinel checking mirrors GCC's own purely-syntactic
behaviour here). A bare, uncast `0` (`int`-typed) also warns, distinctly from
a fully-missing terminator, since it is not guaranteed to zero-fill a
pointer-sized `va_list` slot — matching GCC's `-Wsentinel` strictness.

`sentinel` only makes sense on a variadic function; applying it to a
non-variadic function warns `"sentinel attribute only applies to variadic
functions"` under `-Wattributes` at the declaration, and the (necessarily
argument-less) call sites are not additionally flagged.

```c
void run(const char *path, ...) __attribute__((sentinel));
void run(const char *path, ...) { }

int main(void) {
    run("/bin/ls", "-l", (void*)0);   // ok
    run("/bin/ls", "-l", 0);          // warns: bare 0 is not a pointer; cast NULL / (void*)0
    run("/bin/ls", "-l");             // warns: missing sentinel in function call
}
```

```c
// sentinel(1): one trailing argument is allowed after the NULL
void run2(const char *path, ...) __attribute__((sentinel(1)));
void run2(const char *path, ...) { }

int main(void) {
    run2("/bin/ls", "-l", (void*)0, extra_flag);   // ok
}
```

If the call does not supply enough variadic arguments for the expected
sentinel position to exist at all, CCCC warns `"not enough variable
arguments to fit a sentinel"` instead of indexing past the end of the
argument list.

Diagnosed under `-Wsentinel` (part of `-Wall`); disable with `-Wno-sentinel`.

## `__attribute__((alloc_size(n)))` / `__attribute__((alloc_size(n,m)))` / `[[gnu::alloc_size]]`

Marks a function as returning a pointer to an allocation whose byte size is
given by argument `n` (1-based), or by the product of arguments `n` and `m`
for the two-index form (e.g. `calloc`-style `nmemb * size`). This is the
sole mechanism `__builtin_object_size`/`__builtin_dynamic_object_size` use to
recognize heap allocators — it supersedes an earlier hardcoded name match
against `malloc`/`calloc`/`realloc`/`aligned_alloc`, so any function
(including a project's own arena/pool allocator) participates in heap-size
tracking once annotated, and a function that merely shares an allocator's
*name* but carries no attribute is correctly left untracked:

```c
void *arena_alloc(size_t n) __attribute__((alloc_size(1)));
void *arena_alloc(size_t n) { return malloc(n); }

int main(void) {
    char *p = arena_alloc(64);
    __builtin_object_size(p, 0); // 64 — tracked via the attribute, not the name

    void *my_malloc(int n); // a same-named lookalike with no attribute
    void *q = my_malloc(64);
    __builtin_object_size(q, 0); // (size_t)-1 — conservative, untracked
}
```

libc's `malloc`, `calloc`, `realloc`, `aligned_alloc`, and `reallocarray` are
declared with `alloc_size` in `include/stdlib.h`, so they self-describe the
same way. See the `__builtin_object_size`/`__builtin_dynamic_object_size`
entries in the Builtins table below for the soundness rules (reassignment/
address-of poisoning) that gate when the tracked size is actually trusted.

## `__attribute__((malloc))` / `[[gnu::malloc]]`

Marks a function as returning a fresh, non-aliasing pointer (matching GCC's
`malloc` attribute). Parsed and stored on the function type, but currently
informational only — CCCC does not yet exploit it for aliasing optimizations,
and it deliberately does **not** imply `nonnull`/`returns_nonnull` (a real
allocator can return `NULL` on failure). libc's `malloc`, `calloc`, and
`aligned_alloc` carry it in `include/stdlib.h`; `realloc`/`reallocarray` do
not, since they may return the same block as their input pointer.

The VM has no optimiser, so there is nothing for the attribute to feed:
GCC/Clang exploit it to reorder loads/stores around a call and elide dead
stores, all of which the VM never did. Under `-c=native` the attribute is
serialized verbatim and the host cc acts on it.

## `__attribute__((designated_init))` / `[[gnu::designated_init]]`

Marks a struct type as requiring **every** initializer of that type to use
designated (`.field = value`) syntax rather than positional. This guards
against silent breakage when a struct's field order changes — the classic
use case is Linux-kernel-style ABI-facing structs.

This is a **static, parse-time-only** check over braced initializer lists: any
positional element reaching a `designated_init` struct's member list warns,
including the positional tail of a mixed literal (`{.a = 1, 2}`) and `{0}`.
A brace-less copy-initializer (`struct S a = b;`) is not a positional element
list and is never flagged. C23 empty-init `{}` supplies no elements and is
also silent.

```c
struct point { int x, y; } __attribute__((designated_init));

int main(void) {
    struct point a = {.x = 1, .y = 2};  // ok
    struct point b = {1, 2};            // warns: positional initialization
    struct point c = {.x = 1, 2};       // warns: positional tail
    struct point d = {0};               // warns: positional
    struct point e = {};                // ok: no elements
}
```

Diagnosed under `-Wdesignated-init`. Unlike GCC, this is **opt-in only** — it
is not part of `-Wall`/`-Wextra`, since CCCC otherwise enables no warnings by
default; pass `-Wdesignated-init` explicitly (or promote it with
`-Werror=designated-init`).
