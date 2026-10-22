# Checked Regions

Opening and nesting `_Checked`/`_Unchecked` regions.

## Checked Regions

The incremental-migration half of the checked-pointer layer: a way to say
"inside this region, only checked pointers are allowed" rather than relying
on every declaration to opt in individually. Within a checked region, an
unchecked pointer declaration or an unsafe pointer cast is a compile error,
so the guarantee is local to the region rather than per-declaration, and a
codebase can be migrated block-by-block.

**Four ways to open a region**, all interchangeable:

```c
[[cccc::checked]] void f(void) {
    int * [[cccc::array, cccc::count(n)]] a = ...; // fine
    int *p = a;                                    // error: unchecked local

    [[cccc::unchecked]] {
        int *raw = (int *)a;                       // fine -- inside the escape hatch
    }
}
```

```c
#pragma cccc checked begin
// every declaration and cast below is checked, until the matching end
#pragma cccc checked end
```

```c
void g(void) {
    _Checked {
        int * [[cccc::array, cccc::count(n)]] a = ...; // fine
        _Unchecked {
            int *raw = (int *)a;                       // fine -- escape hatch
        }
    }
}
```

`[[cccc::unchecked]]` is the mirror-image attribute — it opens an *unchecked*
region, most often nested inside a checked one as the incremental-migration
escape hatch for code that cannot yet be made safe. Both attributes accept
the GNU spelling (`__attribute__((checked))`, `__attribute__((unchecked))`,
`__checked__`/`__unchecked__`) and the `@checked`/`@unchecked` short prefix,
matching every other checked-pointer attribute.

**The Checked C keyword spellings** `_Checked { ... }` / `_Unchecked { ... }`
are a fourth, source-compat spelling for code written against Checked C —
they push/pop exactly the same region state as the attribute form, with no
separate representation. They are recognized *positionally*: a name
immediately followed by `{`, in statement position, the same shape a real
tokenizer keyword would occupy — but `_Checked`/`_Unchecked` are **not**
reserved words, so a variable, function, or any other identifier spelled
`_Checked` or `_Unchecked` elsewhere in a program is completely unaffected.
The trade-off is that this form only opens a region as a standalone
statement — `void f(void) _Checked { ... }` (declarator-suffix position) is
not recognized this way; use `[[cccc::checked]]` on the function definition
for that shape instead.

**Nesting.** Regions nest to any depth and the innermost one always wins:
`checked { unchecked { checked { ... } } }` is legal, and each level's
declarations are checked against its own state, not an ancestor's — this
holds across spellings too: an attribute region, a keyword region, and a
pragma region freely nest against one another, and whichever one is
innermost at a given point wins. An attribute- or keyword-introduced region
(on a function definition or a compound statement) always overrides a
surrounding `#pragma cccc checked/unchecked` region for its own lexical
extent.

**The pragma form** is a positional preprocessor directive, not a
`config()` option — `#pragma cccc config(...)` is resolved for the whole
file before parsing begins (see `--checked-pointers` above), so it has no
lexical position and cannot express a region. `#pragma cccc checked begin`
/ `#pragma cccc checked end` (and the `unchecked` pair) instead stamp their
state directly onto the token stream, the same mechanism `#pragma pack(N)`
uses. An unclosed `begin` is a compile error; a stray `end` with no matching
`begin` is too.

**Header contamination.** A region opened around an `#include` does **not**
apply to the header's own declarations — every libc prototype is an
unchecked pointer, and cccc ships no annotated headers to fall back on, so
a checked region spanning `#include <stdio.h>` would otherwise reject every
declaration inside it:

```c
#pragma cccc checked begin
#include <stdio.h>          // fine -- header declarations are exempt
int main(void) {
    printf("hi\n");         // fine -- calling into libc is unaffected
    return 0;
}
#pragma cccc checked end
```

**The v1 ban list — exactly two rules:**

1. Declaring a pointer-typed local, parameter, global, or struct/union
   member whose checked kind is `CHECKED_NONE` (i.e. no
   `[[cccc::single/array/ntarray]]`) is a compile error.
2. A cast to an unchecked pointer type, or a cast that changes an already-
   checked pointer's checked kind (`array` → `single`, etc.), is a compile
   error. `[[cccc::assume]]`/`[[cccc::dynamic]]` (see "Bounds casts" above)
   are exempt from this rule — they ARE the sanctioned way to make exactly
   this conversion inside a checked region, an explicit, auditable
   replacement for reaching for `[[cccc::unchecked]] { ... }` just to
   launder an unchecked pointer through a plain cast.

**Explicitly not banned:** `[[cccc::array]]`/`[[cccc::ntarray]]` with no
bounds form (`count`/`byte_count`/`bounds`) stays legal-but-unchecked, the
same as outside a region — `bounds(unknown)` remains the explicit trust
escape hatch; address-of and array-to-pointer decay producing a bare `T *`;
and calling into a function whose prototype is unchecked (this covers all
of libc — there is no *ban* on this call-boundary, a known v1 gap tracked
as #1336; an annotated prototype's own declared bounds ARE enforced at
such a call under `--checked-pointers`, see "Bounds-safe interfaces" above,
which is orthogonal to whether the call itself is ever diagnosed).
A checked function pointer is meaningless (no bounds form applies to it),
so `TY_FUNC`-pointee pointers are exempt from both rules.

**Always on.** Unlike `CHKR`'s runtime bounds check, these are pure
parse/type-check diagnostics — never gated behind `--checked-pointers`, and
they fire in `-c=native`, `-m`/`--dump-expanded`, and `-c=generated` output
too, the same footing as the single-pointer-arithmetic ban and the bounds
side-effect rejection described above. The two region attributes are
stripped from all of those outputs, ABI-transparently, exactly like the six
checked-pointer attributes; the `_Checked`/`_Unchecked` keyword form carries
no state of its own either, so it likewise leaves nothing behind to strip.

**Known v1 gaps**, deferred to follow-up tickets: checked/unchecked
call-boundary diagnostics (interop) are not enforced; a C23 `auto`-deduced
local (`auto p =
some_unchecked_pointer;`) is not caught by the declaration ban -- the type
check runs against the placeholder `auto` type before the initializer's
deduced type is known, so declare the pointer with an explicit unchecked
type instead of `auto` if the region's diagnostic should catch it; a
mismatched `#pragma cccc checked begin` / `#pragma cccc unchecked end` pair
is accepted rather than rejected (the closing `end` always restores
whichever state preceded its matching `begin`, regardless of which keyword
it was spelled with).
