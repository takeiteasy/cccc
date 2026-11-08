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

int log_[8];
int nlog;

int note(int v) {
    log_[nlog++] = v;
    return v;
}

int twice(int a, int b @ default(a * 2)) {
    return a * 100 + b;
}

[[cccc::test]]
void test_default_names_an_earlier_parameter(void) {
    AssertEq(twice(3), 306);
    AssertEq(twice(3, 1), 301);
    AssertEq(twice(.a = 4), 408);
}

int once(int a, int b @ default(a + a + a)) {
    return b;
}

[[cccc::test]]
void test_earlier_argument_is_evaluated_once(void) {
    ticks = 0;
    AssertEq(once(tick()), 3);
    AssertEq(ticks, 1);
}

int chain(int a @ default(1), int b @ default(a + 1), int c @ default(b + 1)) {
    return a * 100 + b * 10 + c;
}

[[cccc::test]]
void test_defaults_chain_through_defaulted_parameters(void) {
    AssertEq(chain(), 123);
    AssertEq(chain(5), 567);
    AssertEq(chain(.b = 9), 200);
}

int widths(int a, int n @ default(sizeof(a))) {
    return n;
}

int pair_sum(Pair p, int k @ default(p.a + p.b)) {
    return k;
}

int with_literal(int a, Pair p @ default((Pair){.a = a, .b = 2})) {
    return p.a * 10 + p.b;
}

[[cccc::test]]
void test_default_uses_earlier_parameter_in_other_forms(void) {
    AssertEq(widths(1), 4);
    AssertEq(pair_sum((Pair){3, 4}), 7);
    AssertEq(with_literal(4), 42);
}

int sum3(int a, int b, int c @ default(a + b + a)) {
    return c;
}

[[cccc::test]]
void test_arguments_keep_left_to_right_order(void) {
    nlog = 0;
    AssertEq(sum3(note(1), note(2)), 4);
    AssertEq(nlog, 2);
    AssertEq(log_[0], 1);
    AssertEq(log_[1], 2);
}

Pair make_pair(int a, int b @ default(a + 1)) {
    return (Pair){a, b};
}

[[cccc::method(Vec2)]]
int scaled(Vec2 *self, int dx @ default(self->x * 10)) {
    return dx;
}

[[cccc::test]]
void test_struct_return_and_receiver_defaults(void) {
    AssertEq(make_pair(5).b, 6);
    Vec2 v = {2, 3};
    AssertEq(v.scaled(), 20);
}

// The default names the declaration's parameters, not the definition's.
int renamed(int x, int y);
int renamed(int a, int b @ default(a + 1));
int renamed(int p, int q) {
    return q;
}

[[cccc::test]]
void test_default_names_come_from_its_declaration(void) {
    AssertEq(renamed(41), 42);
}

enum Color { RED, GREEN = 7, BLUE };
typedef enum { SMALL = 1, BIG = 40 } Size;

int paint(enum Color c @ default(GREEN)) {
    return c;
}

int pick(Size s @ default(BIG),
         int  n @ default(BLUE * 2 + (int)sizeof(enum Color))) {
    return s + n;
}

[[cccc::test]]
void test_enum_defaults(void) {
    AssertEq(paint(), GREEN);
    AssertEq(paint(.c = BLUE), 8);
    AssertEq(pick(), 40 + 20);
    AssertEq(pick(SMALL), 1 + 20);
}

const char *join(const char *a @ default("x"
                                         "y")) {
    return a;
}

[[cccc::test]]
void test_concatenated_string_default(void) {
    AssertEq(join()[1], 'y');
}

int file_scope_size = sizeof(widths(1));

[[cccc::test]]
void test_naming_default_in_unevaluated_call(void) {
    AssertEq(file_scope_size, 4);
}
