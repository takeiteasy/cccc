# Attribute Syntax

The `@` shorthand, where attributes may appear, and `asm` labels.

## `@` prefix

`@name` and `@name(args)` rewrite to the canonical attribute form before parsing. Resolution is in this order:

| Order | Kind | Rewrites to | Examples |
|---|---|---|---|
| 1 | CCCC-specific | `[[cccc::name]]` | `@comptime`, `@test`, `@test_setup`, `@single`, `@array`, `@ntarray`, `@count(n)`, `@byte_count(n)`, `@bounds(lo,hi)`, `@assume`, `@dynamic`, `@checked`, `@unchecked` |
| 2 | Standard C23 | `[[name]]` | `@nodiscard`, `@maybe_unused` |
| 3 | Custom comptime | The handler registered by `@comptime(attribute("name"))` | `@serialize struct Point { ... };` |
| 4 | GNU or unknown | `__attribute__((name))` | `@packed`, `@aligned(16)` |

```c
@comptime
Node *make_answer(void) { return MakeIntLiteral(42); }

@test(suite = "math")
void test_add(void) { AssertEq(1 + 1, 2); }

struct @packed point { char x; int y; };
```

The ready-made custom attributes (`@serialize`, `@to_string`, `@accessors`, ...) are in [derive.md](derive.md). `@` is accepted wherever the canonical form is.

CCCC-specific attributes also accept `__comptime`, `__comptime__`, `__macro`, `__macro__`, `__test`, `__test__`, `__test_setup`, `__test_setup__`, `__test_teardown` and `__test_teardown__`. The plain names stay ordinary identifiers.

Custom comptime attributes run on file-scope declarations (types, typedefs, functions, globals) and on members of file-scope structs and unions. Their arguments arrive as expression nodes after the target is built; read them with `StringLiteralValue` and `ConstIntValue` ([macros-attributes.md](macros-attributes.md#custom-attributes), [members](macros-attributes.md#member-attributes)). An `@name` with no handler is dropped with `-Wunknown-attributes`, on by default ([warnings.md](warnings.md)). `__declspec` is reserved as a future fourth fallback.

## Output

`-E`, `-m`, `-c=generated` and `-c=native` strip CCCC-only attributes and route markers, so the output is portable C.

| Flag | Effect |
|---|---|
| `--attr-target=auto\|c23\|gnu\|msvc\|strip` | How remaining attributes are printed. `auto` emits standard C23 attributes as `[[...]]` in C23 mode and GNU `__attribute__` otherwise; GNU-only ones such as `packed` stay GNU |
| `--emit-cccc` | Keep `[[cccc::...]]` attributes and route markers, re-emit cccc-only includes, and print checked-pointer qualifiers after the `*`[^emit] |

[^emit]: `test`, `comptime`, `test_setup`, `test_teardown` and `build` are consumed structurally during preprocessing whatever the flag says; `--emit-cccc` only affects attributes that would be stripped cosmetically, such as `[[cccc::emit]]`. With `-c=native` it disables the `PATH` search, so `CCCC_NATIVE_CC` must name a compiler, since a plain `cc` is not guaranteed to tolerate `[[cccc::...]]`.

## Where attributes may appear

| Position | GNU | C23 |
|---|---|---|
| Storage class specifiers | yes | yes |
| Before a declarator | yes | yes |
| After a declarator | yes | yes |
| Before an abstract declarator | yes | yes |
| Struct or union: before the tag, after the body | yes | yes |
| Enum specifier | yes | yes |
| Labels | yes | yes |
| After a parenthesised declarator | yes (the entity) | yes (the type) |
| Statement level | no | no |

```c
int (*handler)(void) __attribute__((aligned(16)));  // the pointer is aligned
int (*rows)[4] __attribute__((aligned(32)));
```

A C23 attribute after a parenthesised declarator applies to the type before it, as in GCC, so `int (*fp)(void) [[gnu::aligned(16)]]` leaves `fp` unaligned.

A function attribute on a function-pointer declarator applies to the function it points to, so calls through the pointer are checked like direct calls:

```c
void (*log_fn)(const char *, ...) __attribute__((format(printf, 1, 2)));
log_fn("%d", "x");  // -Wformat warning under --format-string-checks
```

Covered: `format`, `nonnull`, `returns_nonnull`, `sentinel`, `noreturn`, `warn_unused_result`/`nodiscard`, `pure`, `const`, `malloc`, `alloc_size`, `error`, `warning`, in the GNU or `[[gnu::...]]` spelling.

### Name spellings

`[[__gnu__::name]]` equals `[[gnu::name]]`, and any GNU or standard name may be written `__name__`, as headers do:

```c
int buf[4] [[__gnu__::__aligned__(16)]];  // same as [[gnu::aligned(16)]]
[[__nodiscard__]] int parse(void);        // same as [[nodiscard]]
```

## `asm("symbol")` labels

A GNU asm label after a function declarator keeps the C identifier for lookup and uses the label as the external FFI symbol:

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

Object asm labels are parsed but change neither storage layout nor serialized output.
