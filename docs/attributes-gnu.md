# GNU Function and Allocation Attributes

Purity, optimization, sentinel, allocation and designated-init attributes.

| Attribute | VM effect | Native effect | Diagnostic |
|---|---|---|---|
| `pure` | Informational (no optimiser) | Forwarded to the host `cc` | |
| `const` | Informational | Forwarded | |
| `optimize(...)` | Ignored with `-Wignored-features` | Use `-c=native -O<n>` instead | |
| `sentinel`, `sentinel(N)` | Call-site check | | `-Wsentinel` (in `-Wall`) |
| `alloc_size(n)`, `alloc_size(n,m)` | Marks heap allocators for `__builtin_object_size` | | |
| `malloc` | Informational | Forwarded | |
| `designated_init` | Parse-time check | | `-Wdesignated-init` (opt-in) |

`__has_attribute` returns 1 for `pure`/`__pure__` and `const`/`__const__`; for `optimize` both `__has_attribute` and `__has_c_attribute` return 1.

## `pure` and `const`

`pure` allows reading global state but no side effects; `const` is stronger: the result depends only on the arguments. Both are parsed, stored on the function and visible to `__has_attribute` and reflection.

| Spellings |
|---|
| `__attribute__((pure))`, `__attribute__((__pure__))`, `[[gnu::pure]]`, `[[cccc::pure]]` |
| `__attribute__((const))`, `__attribute__((__const__))`, `[[gnu::const]]`, `[[cccc::const]]` |

`[[const]]` would collide with the keyword, so the C23 form needs `gnu::`.

## `optimize`

Syntax-checked, then ignored with `-Wignored-features`, because there is no optimiser. A malformed argument is still an error.

| Spelling | Accepts |
|---|---|
| `__attribute__((optimize("O2")))` | `"O0"` to `"O4"`, optional leading `-` |
| `[[cccc::optimize(3)]]` | Integer 0 to 4 |
| `@optimize(2)` | Shorthand |

## `sentinel`

A variadic function that needs a `NULL`-terminated argument list (like `execl`). Bare `sentinel` wants the last argument to be `NULL`; `sentinel(N)` allows `N` more arguments after it.

```c
void run(const char *path, ...) __attribute__((sentinel));

int main(void) {
    run("/bin/ls", "-l", (void*)0);   // ok
    run("/bin/ls", "-l", 0);          // warns: bare 0 is not a pointer
    run("/bin/ls", "-l");             // warns: missing sentinel in function call
}

void run2(const char *path, ...) __attribute__((sentinel(1)));
// run2("/bin/ls", "-l", (void*)0, extra_flag);   // ok
```

| Rule | Detail |
|---|---|
| What satisfies it | A literal or constant-folded pointer null: `NULL`, `(void*)0`, `(T*)0`, `nullptr`. A variable holding `NULL` still warns (a syntactic check, like GCC's) |
| Bare `0` | Warns distinctly: it need not fill a pointer-sized slot |
| Too few arguments to reach the sentinel position | "not enough variable arguments to fit a sentinel" |
| Non-variadic function | `-Wattributes` warns at the declaration |

## `alloc_size`

Marks a function as returning an allocation whose byte size is argument `n` (1-based), or the product of `n` and `m` (a `calloc`-style `nmemb * size`). It is how `__builtin_object_size` and `__builtin_dynamic_object_size` recognise heap allocators, so any function, including your own arena allocator, takes part once annotated; a same-named function with no attribute stays untracked. libc's `malloc`, `calloc`, `realloc`, `aligned_alloc` and `reallocarray` carry it.

```c
void *arena_alloc(size_t n) __attribute__((alloc_size(1)));
void *arena_alloc(size_t n) { return malloc(n); }

int main(void) {
    char *p = arena_alloc(64);
    __builtin_object_size(p, 0);   // 64

    void *my_malloc(int n);        // lookalike, no attribute
    __builtin_object_size(my_malloc(64), 0);   // (size_t)-1: untracked
}
```

## `malloc`

Marks a function as returning a fresh, non-aliasing pointer. It is parsed and stored but not exploited by the VM, and it does not imply `nonnull` (an allocator may return `NULL`). Native output serializes it verbatim. libc's `malloc`, `calloc` and `aligned_alloc` carry it; `realloc` and `reallocarray` do not, since they may return their input block.

## `designated_init`

Requires every initializer of a struct type to use `.field = value`. It guards ABI-facing structs against field reordering.

```c
struct point { int x, y; } __attribute__((designated_init));

struct point a = {.x = 1, .y = 2};  // ok
struct point b = {1, 2};            // warns: positional
struct point c = {.x = 1, 2};       // warns: positional tail
struct point d = {0};               // warns
struct point e = {};                // ok: no elements
```

A braceless copy (`struct S a = b;`) is never flagged. Unlike GCC the warning is opt-in: pass `-Wdesignated-init`, or `-Werror=designated-init`.
