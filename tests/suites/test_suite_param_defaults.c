// CCCC_FLAGS: --testing
// Suite: default and keyed arguments at the call site.

#include <cccc/derive.h>

typedef struct {
    int a, b;
} Pair;

int base = 5;
int ticks;

int area(int, int);
int area(int w @ default(80), int h @ default(24));
int area(int w, int h) {
    return w * 100 + h;
}

[[cccc::test]]
void test_omitted_arguments_take_their_defaults(void) {
    AssertEq(area(), 8024);
    AssertEq(area(1), 124);
    AssertEq(area(1, 2), 102);
}

[[cccc::test]]
void test_keyed_arguments_reorder_and_skip(void) {
    AssertEq(area(.h = 10), 8010);
    AssertEq(area(.w = 2), 224);
    AssertEq(area(.h = 1, .w = 2), 201);
    AssertEq(area(7, .h = 9), 709);
}

int mix(int a, int b @ default(2), int c @ default(3)) {
    return a * 100 + b * 10 + c;
}

[[cccc::test]]
void test_keyed_argument_on_a_parameter_without_default(void) {
    AssertEq(mix(.a = 9), 923);
    AssertEq(mix(.c = 1, .a = 4), 421);
}

int total(Pair p @ default((Pair){base, 2}), int k @ default(base * 10)) {
    return p.a + p.b + k;
}

[[cccc::test]]
void test_struct_and_expression_defaults(void) {
    AssertEq(total(), 57);
    AssertEq(total((Pair){1, 1}), 52);
    AssertEq(total(.k = 0), 7);
}

// A default names file-scope declarations; a local of the caller is ignored.
[[cccc::test]]
void test_default_ignores_caller_locals(void) {
    int base = 1000;
    AssertEq(total(), 57);
    AssertEq(base, 1000);
}

int tick(void) {
    return ++ticks;
}

int stamped(int v @ default(tick())) {
    return v;
}

[[cccc::test]]
void test_default_is_evaluated_once_per_call(void) {
    ticks = 0;
    AssertEq(stamped(), 1);
    AssertEq(stamped(), 2);
    AssertEq(stamped(40), 40);
    AssertEq(ticks, 2);
}

int label_len(const char *s @ default("untitled")) {
    int n = 0;
    while (s[n])
        n++;
    return n;
}

[[cccc::test]]
void test_string_default(void) {
    AssertEq(label_len(), 8);
    AssertEq(label_len("ab"), 2);
}

int first_of(int n @ default(3), ...) {
    return n;
}

[[cccc::test]]
void test_default_before_variadic_arguments(void) {
    AssertEq(first_of(), 3);
    AssertEq(first_of(4, 5, 6), 4);
}

typedef struct {
    int x, y;
} Vec2;

[[cccc::method(Vec2)]]
int shifted(Vec2 *self, int dx @ default(10), int dy @ default(20)) {
    return self->x + dx + (self->y + dy) * 1000;
}

[[cccc::test]]
void test_method_call_defaults(void) {
    Vec2 v = {1, 2};
    AssertEq(v.shifted(), 22011);
    AssertEq(v.shifted(.dy = 0), 2011);
}

int by_pointer(int (*f)(int, int)) {
    return f(3, 4);
}

[[cccc::test]]
void test_function_pointers_take_every_argument(void) {
    AssertEq(by_pointer(area), 304);
    int (*fp)(int, int) = area;
    AssertEq(fp(5, 6), 506);
}
