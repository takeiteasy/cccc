# Return Value Assertions

`return = value` checks what a test function returns: integers, floats, strings, structs and unions.

Use `return = value` to assert that a test function returns a specific value. The comparison type is inferred from the literal:

**Integer** (and `char`/`enum`): the function's return value is compared as a 64-bit integer. The operand is any integer constant expression: a literal, an enum name, a `#define` name, a character literal, or a cast, `sizeof` or arithmetic over those.

```c
enum Color { RED, GREEN };
#define ANSWER 42

[[cccc::test(return = ANSWER)]]
int test_the_answer(void) {
    return 6 * 7;
}

[[cccc::test(return = GREEN)]]
int test_enum(void) {
    return GREEN;
}

[[cccc::test(return = 'A')]]
int test_char_a(void) {
    return 65;
}

[[cccc::test(return = -1)]]
int test_neg(void) {
    return -1;
}
```

Names resolve against what is declared above the test function. An undeclared name or an operand that is not a constant (such as a function call) is a compile error:

```
error: undefined variable 'LIME'
```

**Float/double**: the return value is compared within an absolute tolerance of `1e-9` for `=`/`!=`. For ordered comparisons (`<`, `>`, etc.) no tolerance is applied.

```c
[[cccc::test(return = 3.14)]]
double test_pi(void) {
    return 3.14;
}
```

Use `return_epsilon` to override the tolerance for a specific test:

```c
[[cccc::test(return = 3.14159, return_epsilon = 1e-5)]]
double test_approx_pi(void) {
    return compute_pi();  // passes if within 1e-5 of 3.14159
}
```

`return_epsilon` only applies to `=` and `!=`; ordered comparisons use exact values regardless.

**String** (`char *`): the returned pointer is compared to the literal using `strcmp`.

```c
[[cccc::test(return = "ok")]]
const char *test_status(void) {
    return "ok";
}
```

**Struct / union**: use an inline compound literal to assert a struct or union return value. The literal's type is `struct TAG`, `union TAG` or a typedef name, and it must be the function's return type; the function must return a struct or union, and any other type is a compile error. Fields are compared field-by-field using the same per-type rules as the scalar paths (epsilon for `float`/`double`, `strcmp` for `char *`, integer equality for all other types). Fields omitted from the compound literal are expected to be zero (C zero-initialisation), with one exception — see **Unions**, below.

```c
struct Point { int x; int y; };

[[cccc::test(return = (struct Point){.x = 1, .y = 2})]]
struct Point test_make_point(void) {
    return (struct Point){.x = 1, .y = 2};
}
```

Mixed field types are supported:

```c
struct Named { char *label; int code; };

[[cccc::test(return = (struct Named){.label = "ok", .code = 0})]]
struct Named test_result(void) {
    return (struct Named){.label = "ok", .code = 0};
}
```

Struct assertions support `=` and `!=` operators only; ordered comparisons (`<`, `<=`, `>`, `>=`) are not meaningful for structs and are a compile error.

Bit-field and `long double` members compare by value, and `--testing=native` checks struct and union assertions the same way as the VM, with the same failure message.

If the assertion fails:

```
expected return value = (struct Point){.x = 1, .y = 2}, got {.x = 99, .y = 2}
```

**Nested fields**: a field that is itself a struct, union, or array can be asserted with a nested compound literal. The nested literal may repeat the `(struct|union TAG)` or `(TYPEDEF)` prefix or, more commonly, be a bare brace list — its type is inferred from the field being initialized. A prefix must name the type of the member or array element it initialises; a different type, or a member that is not a struct or union, is a compile error. Nesting recurses to a maximum of 8 levels; a literal nested deeper than that is a compile error.

```c
struct Inner { int a; int b; };
struct Outer { struct Inner p; int z; };

[[cccc::test(return = (struct Outer){.p = {.a = 1, .b = 2}, .z = 3})]]
struct Outer make_outer(void) {
    return (struct Outer){.p = {.a = 1, .b = 2}, .z = 3};
}
```

A field omitted from a nested literal is expected to be zero, recursively — an entirely omitted nested field means all of its own fields are expected to be zero.

**Anonymous struct/union members**: an anonymous member's fields are addressed directly by the outer struct's own designators, matching C's own anonymous-member lookup rules — there is no extra level of nesting in the literal.

```c
struct WithAnon {
    struct { int x; int y; }; // anonymous
    int z;
};

[[cccc::test(return = (struct WithAnon){.x = 1, .y = 2, .z = 3})]]
struct WithAnon make(void) {
    return (struct WithAnon){.x = 1, .y = 2, .z = 3};
}
```

**Arrays**: array-typed fields are asserted with a brace list of positional (undesignated) elements, matched left-to-right; elements past the end of the list are expected to be zero. A `char[]` field may instead be compared against a string literal, following C zero-initialisation semantics (bytes past the literal's terminating `NUL` are expected to be zero).

```c
struct Buf { int values[5]; char label[8]; };

[[cccc::test(return = (struct Buf){.values = {1, 2, 3}, .label = "hi"})]]
struct Buf make_buf(void) {
    struct Buf r = {0};
    r.values[0] = 1; r.values[1] = 2; r.values[2] = 3;
    r.label[0] = 'h'; r.label[1] = 'i';
    return r;
}
```

A typedef name is the only way to name an anonymous struct:

```c
typedef struct { int x, y; } Pt;

[[cccc::test(return = (Pt){.x = 3, .y = 4})]]
Pt test_typedef(void) { return (Pt){3, 4}; }
```

The name must be a typedef of a struct or union that matches the return type; anything else is a compile error.

An array of structs takes a positional list of (optionally `(struct TAG)` or `(TYPEDEF)` prefixed) nested literals: `.items = {(struct Pair){.a=1,.b=2}, (struct Pair){.a=3,.b=4}}`.

**Unions**: union arms alias the same storage, so — unlike a struct — an arm *not* named in the literal is not asserted on at all (it is not "expected zero", since reading it back would just reinterpret whichever arm actually was asserted). Only the arms named in the literal are compared.

```c
union Val { int i; float f; };

[[cccc::test(return = (union Val){.i = 0x3f800000})]]
union Val make_val(void) {
    return (union Val){.i = 0x3f800000}; // .f is not checked
}
```

Field values are constant expressions too, so `(struct Pair){.a = GREEN, .b = 6 * 7}` works.

**Pointers**: a pointer field or return value can be asserted null or non-null. `NULL`, `nullptr` and `0` assert null; `!=` asserts non-null. An address cannot be asserted, so any other pointer constant is a compile error (a struct field with a non-null integer fails the test).

```c
struct Node { int val; struct Node *next; };

[[cccc::test(return = (struct Node){.val = 1, .next = NULL})]]
struct Node last(void) { return (struct Node){1, NULL}; }

[[cccc::test(return = (struct Node){.val = 1, .next != NULL})]]
struct Node middle(void) { static struct Node t; return (struct Node){1, &t}; }

[[cccc::test(return != NULL)]]
int *find(void) { static int x; return &x; }
```

`!=` after a field name works on any scalar field (`.val != 2`), not on a nested literal.

**Scope limitations** — the following are not supported and are deferred to follow-up tickets:

- *Bitfields* — a bitfield member is compared as its whole storage unit, not its individual bit-width.

**Comparison operators** (`=`, `!=`, `<`, `<=`, `>`, `>=`): use a comparison operator instead of equality. The default (no operator) is `=`.

```c
[[cccc::test(return > 0)]]
int test_positive(void) {
    return 42;
}

[[cccc::test(return != 0)]]
int test_nonzero(void) {
    return -1;
}
```

If the assertion fails, the test is reported as failed with a message showing the expected and actual values:

```
expected return value = 42, got 7
expected return string = "ok", got "err"
```

`return` may be combined with other options:

```c
[[cccc::test(return = 42, name = "answer is correct", suite = "math")]]
int test_the_answer(void) {
    return 6 * 7;
}
```

`Assert*` macros and the `return` assertion are independent — both must pass for the test to pass.
