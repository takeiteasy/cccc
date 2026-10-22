# Attribute Syntax

The `@` prefix, where attributes may appear, and `asm` labels.

## `@`-prefix Attribute Syntax

CCCC supports a concise `@name` / `@name(args)` shorthand that rewrites to
the canonical attribute form before parsing:

| Usage | Rewrites to | Example |
|-------|-------------|---------|
| `@name` (CCCC-specific) | `[[cccc::name]]` | `@comptime`, `@test`, `@test_setup`, `@single`, `@array`, `@ntarray`, `@count(n)`, `@byte_count(n)`, `@bounds(lo,hi)`, `@assume`, `@dynamic`, `@checked`, `@unchecked` |
| `@name` (standard C23) | `[[name]]` | `@nodiscard`, `@maybe_unused` |
| `@name` (GNU / unknown) | `__attribute__((name))` | `@packed`, `@aligned(16)` |
| `@name` (custom comptime) | handler registered by `@comptime(attribute("name"))` | `@serialize struct Point { ... };` |

The ready-made custom attributes (`@serialize`, `@enum_to_string`,
`@accessors`, ...) are listed in [derive.md](derive.md).

Resolution order: CCCC-specific attributes are checked first (they become
`[[cccc::name(...)]]`), then standard C23 attributes (they become
`[[name(...)]]`), then registered custom comptime attributes, then GNU
attributes (they become `__attribute__((name(...)))`). Unrecognised names fall
back to the GNU form.

CCCC-specific attributes also accept double-underscore keyword aliases:
`__comptime`, `__comptime__`, `__macro`, `__macro__`, `__test`, `__test__`,
`__test_setup`, `__test_setup__`, `__test_teardown`, and
`__test_teardown__`. Plain names such as `comptime`, `macro`, and `test`
remain ordinary identifiers.

Generated and preprocessed output never includes CCCC-specific syntax by
default. `-E`, `-m`, `-c=generated`, and `-c=native` strip CCCC-only
attributes and route markers before emitting C for another compiler. Use
`--attr-target=auto|c23|gnu|msvc|strip` to select how remaining attributes
are printed. `auto` emits standard C23 attributes as `[[...]]` in C23 mode
and uses GNU `__attribute__((...))` otherwise; GNU-only attributes such as
`packed` stay GNU in `auto` mode.

Pass `--emit-cccc` to invert this: `[[cccc::...]]` attributes and route
markers are preserved verbatim, cccc-only `#include`d files are re-emitted
instead of dropped, and checked-pointer qualifiers
(`[[cccc::single/array/ntarray]]`, `count()`/`byte_count()`/`bounds()`) are
serialized in their post-`*` declarator position instead of being omitted.
`[[cccc::test]]`/`comptime`/`test_setup`/`test_teardown`/`build` are
consumed structurally during preprocessing regardless of this flag (they
trigger real compiler behaviour, not just cosmetic attribute spelling) --
`--emit-cccc` only affects attributes that would otherwise be stripped
cosmetically, such as `[[cccc::emit]]`. With `-c=native`, `--emit-cccc`
still hands the (dialect) source to a real system compiler, but disables
the usual `cc`/`clang`/`gcc` PATH search: `CCCC_NATIVE_CC` must name a
compiler explicitly, since a plain `cc` cannot parse `[[cccc::...]]`
syntax (it degrades gracefully to an ignored unknown-attribute warning
under Clang/GCC's C23 attribute handling, but is not guaranteed to under
every compiler).

```c
@comptime
Node *make_answer(void) { return MakeIntLiteral(42); }

@test(suite = "math")
void test_add(void) { AssertEq(1 + 1, 2); }

struct @packed point { char x; int y; };
```

The `@` prefix is accepted wherever the corresponding canonical form is valid.
Custom comptime attributes run on file-scope declarations (types, typedefs,
functions, and globals) and on members of file-scope structs and unions. Their
arguments are parsed as expression AST nodes and passed to the registered
handler after the decorated target has been built; the handler reads them with
`StringLiteralValue` and `ConstIntValue` ([macros.md](macros-attributes.md#custom-attributes)). Member attributes are
described in [macros.md](macros-attributes.md#member-attributes). An `@name` with no
registered handler is dropped with a `-Wunknown-attributes` warning, which is
on by default ([warnings.md](warnings.md)).
`__declspec` (Windows) is reserved as a future fourth fallback category.

## Position in Grammar

Attributes are accepted at these positions in the grammar:

| Position | GNU `__attribute__` | C23 `[[...]]` |
|----------|---------------------|---------------|
| Storage class specifier sequence | ✓ | ✓ |
| Before declarator (prefix) | ✓ | ✓ |
| After declarator (suffix) | ✓ | ✓ |
| Before abstract declarator | ✓ | ✓ |
| Struct/union — before tag | ✓ | ✓ |
| Struct/union — after body | ✓ | ✓ |
| Enum specifier | ✓ | ✓ |
| Labels | ✓ | ✓ |
| After a parenthesised declarator | ✓ | ✓ (applies to the type) |
| Statement level | ✗ | ✗ |

An attribute after a parenthesised declarator applies to the declared entity:

```c
int (*handler)(void) __attribute__((aligned(16)));  // the pointer is aligned
int (*rows)[4] __attribute__((aligned(32)));
```

A C23 attribute in that position appertains to the type just before it, as in
GCC, so `int (*fp)(void) [[gnu::aligned(16)]]` leaves `fp` unaligned.

A function attribute on a function-pointer declarator applies to the function
it points to, so calls through the pointer are checked like direct calls:

```c
void (*log_fn)(const char *, ...) __attribute__((format(printf, 1, 2)));
log_fn("%d", "x");  // -Wformat warning under --format-string-checks
```

This covers `format`, `nonnull`, `returns_nonnull`, `sentinel`, `noreturn`,
`warn_unused_result`/`nodiscard`, `pure`, `const`, `malloc`, `alloc_size`,
`error` and `warning`, in either the GNU or the `[[gnu::...]]` spelling.

### Scope and name spellings

`[[__gnu__::name]]` is the same as `[[gnu::name]]`, and every GNU or standard
attribute name may be written in its reserved `__name__` form, as headers do
to stay safe from user macros:

```c
int buf[4] [[__gnu__::__aligned__(16)]];  // same as [[gnu::aligned(16)]]
[[__nodiscard__]] int parse(void);        // same as [[nodiscard]]
```

## GNU `asm("symbol")` Labels

GNU asm labels are accepted after function declarators. CCCC keeps the C
identifier for source lookup and uses the label as the external FFI symbol:

```c
typedef int Print(const char *);

Print say asm("puts");

int main(void) {
    Print a asm("puts"), b asm("puts");
    say("file scope");
    a("first");
    b("second");
}
```

Object asm labels are parsed as declaration syntax but do not change storage
layout or serialized output.
