# Cleanup, Constructor and Destructor

Scope-exit cleanup and program-lifetime hooks.

## `__attribute__((cleanup(fn)))` / `[[gnu::cleanup(fn)]]`

Registers a scope-exit callback `fn` for a local variable. When the variable
goes out of scope, `fn(&var)` is called automatically. `fn` must have the
signature `void fn(T *)` where `T` is the type of the variable.

```c
void cleanup_free(int **p) { free(*p); }

void example(void) {
    int *buf __attribute__((cleanup(cleanup_free))) = malloc(100 * sizeof(int));
    // buf is automatically freed when example() returns or the block exits
}
```

**What it applies to:** automatic variables only. On a declarator it covers
that variable; at the start of a declaration it covers every declarator:

```c
__attribute__((cleanup(rel))) int a, b;  // rel(&b), rel(&a)
int c __attribute__((cleanup(rel))), d;  // rel(&c) only
int *__attribute__((cleanup(rel))) p;    // rel(&p): after `*` it names p
__typeof__(c) e;                         // no cleanup: not inherited
int v[n] __attribute__((cleanup(rel)));  // VLA: rel(&v), a pointer to the array
```

As in GCC, the attribute is ignored on a typedef, struct member, parameter,
function, global or `static` local (`-Wattributes` warns `'cleanup' attribute
ignored`). In a type-name (cast, compound literal, `sizeof`, `typeof`), or as
a C23 `[[gnu::cleanup(fn)]]` after the type specifier or a `*`, it applies to
the type and is ignored (`'cleanup' attribute does not apply to types`).

**Call order:** LIFO — the last-declared variable is cleaned up first within a
scope. A jump out of a scope cleans up only the variables whose declaration it
has already passed:

```c
for (int i = 0; i < n; i++) {
    if (skip(i))
        break;  // buf not declared yet: no cleanup
    int *buf __attribute__((cleanup(cleanup_free))) = malloc(64);
}
```

**Scope exit paths covered:**

| Exit kind | Cleanup fires? |
|-----------|---------------|
| Natural block end (`}`) | ✓ |
| `return` | ✓ |
| `break` | ✓ |
| `continue` | ✓ |
| Named `goto` out of scope | ✓ |
| End of a `for` loop declaring the variable (`for (CLEANUP T v = ...; ...)` or `for (T v CLEANUP = ...; ...)`) | ✓ |
| End of a statement expression `({ ... })` (its value is copied first) | ✓ |
| `longjmp` | ✗ (matches GCC C-mode behavior) |

**Return value preservation:** when a non-void return is combined with cleanup
calls, the return value is preserved across cleanup invocations. For integer or
pointer returns, the value is saved via a stack push; for float/double returns,
a dedicated stack slot is used.

Under `-c=native`, `-m` and `-c=generated`, a cleanup variable is declared
where it appears in the source with the host compiler's own `cleanup`
attribute, so the native program runs the same cleanups.

**Static inline cleanup functions** referenced only through the attribute are
kept alive (not dead-stripped) by the liveness pass.

**Goto and scope ancestry:** every named `goto` cleans up exactly the cleanup
scopes between it and the lowest common ancestor it shares with the target label.
This covers cross-sibling jumps (from one block into a sibling at the same
nesting depth) — the source block's variables are still cleaned. Jumping *into*
the scope of a cleanup variable (past its declaration) is ill-formed C and is
diagnosed under `-Wattributes`, since the variable would be uninitialized when
its cleanup runs.

**Limitations:**

- `longjmp` does not trigger cleanup, matching GCC C-mode behavior.

**`__has_attribute`:** returns `1` for `cleanup`.

---

## `__attribute__((constructor))` / `__attribute__((destructor))` / `[[gnu::constructor]]` / `[[gnu::destructor]]`

Registers a `void(void)` function to run before `main()` (constructor) or
after `main()` returns or an explicit `exit()` call (destructor). An
optional integer priority controls relative ordering among multiple
constructors/destructors:

```c
__attribute__((constructor)) void init(void) { /* runs before main() */ }
__attribute__((constructor(101))) void init_early(void) { /* lower number runs first */ }

__attribute__((destructor)) void fini(void) { /* runs after main() returns or exit() is called */ }
[[gnu::destructor(101)]] void fini_late(void) { /* higher number runs first */ }
```

**Ordering:** constructors run in ascending priority order (lower numbers
first); functions with no explicit priority form the default group and run
last. Destructors run in the reverse order — descending priority (higher
numbers first), with the default group running first. A simple stable sort
is used; CCCC does not replicate full ELF `.init_array` priority-band
semantics.

**Limitations:**

- Destructors run on a **normal return from `main()`** and on an explicit
  guest **`exit()`** call, in that same atexit-handlers-then-destructors
  order either way. `_Exit()` and `quick_exit()` do **not** run destructors
  (or atexit/at_quick_exit handlers) — matching GCC, which documents
  destructors as running "after `main` completes or `exit` is called", and
  ISO C (C23 7.24.4.4/7.24.4.7: `_Exit`/`quick_exit` terminate without
  running atexit handlers). `abort()` does not run them either.
- Constructors and destructors must have signature `void(void)` — the GCC
  extension allowing constructors to receive `argc`/`argv`/`envp` is not
  supported.
- A constructor's writes to `_Thread_local` variables are not visible to
  `main()` or later destructors: each of these runs as an independent
  `cc_run_at` cycle with its own freshly-copied TLS segment. Non-TLS globals
  are unaffected — they live in the shared data segment.
- Constructors/destructors only run around `main()` (`cc_run`). `-t`/
  `--testing` mode (`[[cccc::test]]` discovery) invokes test functions
  directly and does not go through `cc_run`, so they do not run in that mode.

**`__has_attribute`:** returns `1` for `constructor` and `destructor`.

**`-c=native`:** the attribute is lowered verbatim (as a prefix on the
declarator, both on the forward declaration and the definition) — the real
host `cc`'s own libc startup/exit machinery drives it there, not CCCC's own
`cc_run_at` ordering code. The VM's documented ordering above was checked
directly against real GNU/clang semantics and already matches.

---
