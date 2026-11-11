# Attribute Syntax

Attribute spellings (`@`, `[[...]]`, `__attribute__`), where attributes may appear, and `asm` labels.

## `@` prefix

`@name` and `@name(args)` rewrite to the canonical attribute form before parsing. Resolution is in this order:

| Order | Kind | Rewrites to | Examples |
|---|---|---|---|
| 1 | CCCC-specific | `[[cccc::name]]` | `@comptime`, `@test`, `@test_setup`, `@single`, `@array`, `@ntarray`, `@count(n)`, `@byte_count(n)`, `@bounds(lo,hi)`, `@assume`, `@dynamic`, `@checked`, `@unchecked` |
| 2 | Standard C23 | `[[name]]` | `@nodiscard`, `@maybe_unused` |
| 3 | Custom comptime | The handler registered by `@comptime(attribute("name"))` | `@serialize struct Point { ... };` |
| 4 | GNU or unknown | `__attribute__((name))` | `@packed`, `@aligned(16)` |

`@template`, `@specialize` and `@inst` are not attributes: the compiler handles them before this table applies, and each has spellings without `@` ([below](#template-marker-spellings), [templates.md](templates.md#marker-spellings)).

```c
@comptime
Node *make_answer(void) { return MakeIntLiteral(42); }

@test(suite = "math")
void test_add(void) { AssertEq(1 + 1, 2); }

struct @packed point { char x; int y; };
```

The ready-made custom attributes (`@serialize`, `@to_string`, `@accessors`, ...) are in [derive.md](derive.md). `@` is accepted wherever the canonical form is.

CCCC-specific attributes also accept `__comptime`, `__comptime__`, `__test`, `__test__`, `__test_setup`, `__test_setup__`, `__test_teardown` and `__test_teardown__`. The plain names stay ordinary identifiers.

Custom comptime attributes run on file-scope declarations (types, typedefs, functions, globals) and on members of file-scope structs and unions. Their arguments arrive as expression nodes after the target is built; read them with `StringLiteralValue` and `ConstIntValue` ([macros-attributes.md](macros-attributes.md#custom-attributes), [members](macros-attributes.md#member-attributes)). An `@name` with no handler is dropped with `-Wunknown-attributes`, on by default ([warnings.md](warnings.md)). `__declspec` is reserved as a future fourth fallback.

## Output

`-E` prints each attribute in its home namespace, whatever namespace it was written in, and drops CCCC-only attributes and route markers, so the output is portable C. `-m`, `-c=generated` and `-c=native` print only the attributes the output needs, in GNU spelling[^serializer].

| Home | `-E` prints | Examples |
|---|---|---|
| CCCC | Nothing (`[[cccc::name]]` with `--emit-cccc`) | `[[single]]`, `[[gnu::single]]`, `[[cccc::test]]` |
| Standard C23 | `[[name]]` in C23 mode, else `__attribute__((name))` | `[[gnu::nodiscard]]` prints `[[nodiscard]]` |
| GNU | `__attribute__((name))` | `[[cccc::packed]]` and `[[clang::cold]]` print `__attribute__((packed))` and `__attribute__((cold))` |
| Unknown name in a namespace | `[[ns::name]]` as written | `[[acme::foo(1)]]` |

A list that mixes homes prints one group per spelling, `[[...]]` first.

| Flag | Effect |
|---|---|
| `--attr-target=auto\|c23\|gnu\|msvc\|strip` | How `-E` prints the remaining attributes. `auto` is the table above; `c23` prints GNU ones as `[[gnu::name]]`; `gnu` always uses `__attribute__`; `msvc` uses `__declspec`; `strip` drops them. Without `-E` it warns on the command line |
| `--emit-cccc` | Keep `[[cccc::...]]` attributes and route markers, re-emit cccc-only includes, print checked-pointer qualifiers after the `*`, and keep `_Checked[N]`/`_Nt_checked[N]` array declarators[^emit] |

[^serializer]: `--attr-target` does not change them: a packed struct is `} __attribute__((packed));` whichever namespace it was written in.
[^emit]: `test`, `comptime`, `test_setup`, `test_teardown` and `build` are consumed structurally during preprocessing whatever the flag says; `--emit-cccc` only affects attributes that would be stripped cosmetically, such as `[[cccc::emit]]`. With `-c=native` it disables the `PATH` search, so `CCCC_NATIVE_CC` must name a compiler, since a plain `cc` is not guaranteed to tolerate `[[cccc::...]]`. A checked array keeps its keyword only where it can be spelled again, as a plain declared name (`int a _Checked[4]`); in other positions, such as a pointer to a checked array, it prints as a plain array.

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

### Namespaces

An attribute resolves by its name alone, in any spelling and any namespace, so every row below is the same attribute:

| Spelling | Example |
|---|---|
| Unscoped | `[[aligned(16)]]`, `[[single]]` |
| `gnu::` / `__gnu__::` | `[[gnu::aligned(16)]]`, `[[gnu::single]]` |
| `cccc::` | `[[cccc::aligned(16)]]`, `[[cccc::single]]` |
| Another vendor | `[[clang::aligned(16)]]`, `[[acme::packed]]` |
| `__attribute__` | `__attribute__((aligned(16)))` |
| `@` | `@aligned(16)` |

An unknown name in a namespace is ignored with a `-Wattributes` warning. `__has_c_attribute(ns::name)` reports a known name in any namespace.

`kernel`, `trusted`, `operator` and `method` need no `cccc::`; the OpenCL address spaces `global`, `local`, `constant`, `private` and `generic` keep it.

### Template marker spellings

The template markers have spellings without `@`, for sources where `@` is taken:

| Marker | `@` | Attribute | Keyword |
|---|---|---|---|
| Template definition | `@template(T)` | `[[cccc::template(T)]]` | `__template(T)` |
| Specialization | `@specialize(T)` | `[[cccc::specialize(T)]]` | `__specialize(T)` |
| Instance | `@inst(T) Name` | | `__inst(T) Name` |

The attribute form takes any namespace or none, and `__attribute__((template(T)))`. It must be the only attribute in its list. `__template__`, `__specialize__` and `__inst__` are accepted too. The postfix instance `Name@(T)` is written with `@` only.

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
