// CCCC_FLAGS: --testing
// Suite: an operator in a template is bound for each instance: a function
// template with [[cccc::operator]] is instantiated from the operand types, and
// an operator written in a struct template's body belongs to every instance.
#pragma cccc suite begin "operator_templates"

@template(T) struct Vec2 { T x, y; };
@template(A, B) struct Pair { A a; B b; };
@template(T, int N) struct Arr { T v[N]; };

@template(T) [[cccc::operator(+)]] @inst(T) Vec2 vadd(@inst(T) Vec2 a, @inst(T) Vec2 b) {
    return (@inst(T) Vec2){a.x + b.x, a.y + b.y};
}

@template(T) [[cccc::operator(-)]] @inst(T) Vec2 vneg(const @inst(T) Vec2 *a) {
    return (@inst(T) Vec2){-a->x, -a->y};
}

@template(T) [[cccc::operator(*)]] @inst(T) Vec2 vscale(@inst(T) Vec2 a, T s) {
    return (@inst(T) Vec2){a.x * s, a.y * s};
}

@template(T) [[cccc::operator(*)]] @inst(T) Vec2 vscale_left(T s, @inst(T) Vec2 a) {
    return (@inst(T) Vec2){a.x * s, a.y * s};
}

@template(T) [[cccc::operator(==)]] bool veq(@inst(T) Vec2 *a, @inst(T) Vec2 *b) {
    return a->x == b->x && a->y == b->y;
}

@template(A, B) [[cccc::operator(+)]] @inst(A, B) Pair pair_add(@inst(A, B) Pair a, @inst(A, B) Pair b) {
    return (@inst(A, B) Pair){a.a + b.a, a.b + b.b};
}

@template(T, int N) [[cccc::operator([])]] T *arr_at(@inst(T, N) Arr *a, int i) {
    return &a->v[i];
}

@template(T, int N) [[cccc::operator(-)]] T arr_diff(@inst(T, N) Arr a, @inst(T, N) Arr b) {
    return a.v[N - 1] - b.v[N - 1];
}

typedef struct { int x, y; } Plain;

// A concrete operator beats a template one for the same operands.
@template(T) [[cccc::operator(%)]] @inst(T) Vec2 vmod_t(@inst(T) Vec2 a, @inst(T) Vec2 b) {
    return (@inst(T) Vec2){0, 0};
}
[[cccc::operator(%)]] @inst(int) Vec2 vmod_int(@inst(int) Vec2 a, @inst(int) Vec2 b) {
    return (@inst(int) Vec2){a.x % b.x, a.y % b.y};
}

[[cccc::test]] void test_binary_by_value(void) {
    @inst(int) Vec2 a = {3, 4}, b = {10, 20};
    @inst(int) Vec2 c = a + b;
    AssertEq(c.x, 13);
    AssertEq(c.y, 24);
}

[[cccc::test]] void test_one_template_two_instances(void) {
    @inst(int) Vec2 a = {1, 2};
    @inst(double) Vec2 d = {0.5, 1.5};
    @inst(int) Vec2 ia = a + a;
    @inst(double) Vec2 id = d + d;
    AssertEq(ia.y, 4);
    AssertEq(id.x, 1.0);
    AssertEq(id.y, 3.0);
}

[[cccc::test]] void test_unary_by_address(void) {
    @inst(int) Vec2 a = {3, -4};
    @inst(int) Vec2 n = -a;
    AssertEq(n.x, -3);
    AssertEq(n.y, 4);
}

[[cccc::test]] void test_binary_minus_and_unary_minus_coexist(void) {
    @inst(long) Vec2 a = {5, 6};
    @inst(long) Vec2 n = -(-a);
    AssertEq(n.x, 5);
}

[[cccc::test]] void test_scalar_operand_either_side(void) {
    @inst(int) Vec2 a = {3, 4};
    @inst(int) Vec2 l = a * 2;
    @inst(int) Vec2 r = 5 * a;
    AssertEq(l.y, 8);
    AssertEq(r.x, 15);
}

[[cccc::test]] void test_comparison_and_ne_fallback(void) {
    @inst(int) Vec2 a = {1, 2}, b = {1, 2}, c = {9, 9};
    AssertTrue(a == b);
    AssertFalse(a == c);
    AssertTrue(a != c);
    AssertFalse(a != b);
}

[[cccc::test]] void test_two_parameter_template(void) {
    @inst(int, double) Pair p = {1, 0.5}, q = {2, 0.25};
    @inst(int, double) Pair s = p + q;
    AssertEq(s.a, 3);
    AssertEq(s.b, 0.75);
}

[[cccc::test]] void test_integer_parameter_deduced(void) {
    @inst(int, 3) Arr a = {{1, 2, 3}};
    @inst(char, 2) Arr b = {{4, 5}};
    *(&a[1]) = 20;
    AssertEq(a.v[1], 20);
    b[0] = 7;
    AssertEq(b.v[0], 7);
    AssertEq(a - a, 0);
}

[[cccc::test]] void test_concrete_operator_wins(void) {
    @inst(int) Vec2 a = {7, 9}, b = {4, 5};
    @inst(int) Vec2 m = a % b;
    AssertEq(m.x, 3);
    AssertEq(m.y, 4);
    @inst(long) Vec2 la = {7, 9}, lb = {4, 5};
    @inst(long) Vec2 lm = la % lb;
    AssertEq(lm.x, 0);
}

[[cccc::test]] void test_instance_reused_across_uses(void) {
    @inst(int) Vec2 a = {1, 1};
    @inst(int) Vec2 c = a + a + a + a;
    AssertEq(c.x, 4);
}

// An operator written in a struct template's body.

@template(T) struct Cell {
    T v;
    struct Cell operator+(struct Cell *a, struct Cell *b) { return (struct Cell){a->v + b->v}; }
    struct Cell operator*(struct Cell a, struct Cell b) { return (struct Cell){a.v * b.v}; }
    struct Cell operator-(struct Cell a) { return (struct Cell){-a.v}; }
};

[[cccc::test]] void test_body_operator_by_address(void) {
    @inst(int) Cell a = {3}, b = {4};
    AssertEq((a + b).v, 7);
}

[[cccc::test]] void test_body_operator_by_value(void) {
    @inst(int) Cell a = {3}, b = {4};
    AssertEq((a * b).v, 12);
}

[[cccc::test]] void test_body_operator_unary(void) {
    @inst(double) Cell a = {2.5};
    AssertEq((-a).v, -2.5);
}

[[cccc::test]] void test_body_operator_each_instance(void) {
    @inst(int) Cell i = {1};
    @inst(long) Cell l = {2};
    AssertEq((i + i).v, 2);
    AssertEq((l + l).v, 4);
}

@template(T) typedef struct Boxed {
    T v;
    struct Boxed operator+(struct Boxed a, struct Boxed b) { return (struct Boxed){a.v + b.v}; }
} Boxed;

[[cccc::test]] void test_body_operator_typedef_template(void) {
    @inst(int) Boxed a = {20}, b = {22};
    AssertEq((a + b).v, 42);
}

// The same applies to a plain struct completed after a forward declaration.

struct Fwd;
struct Fwd {
    int x;
    struct Fwd operator+(struct Fwd *a, struct Fwd *b) { return (struct Fwd){a->x + b->x}; }
};

typedef struct TFwd TFwd;
struct TFwd {
    int x;
    TFwd operator+(TFwd *a, TFwd *b) { return (TFwd){a->x + b->x}; }
};

[[cccc::test]] void test_forward_declared_struct_operator(void) {
    struct Fwd a = {5};
    AssertEq((a + a).x, 10);
}

[[cccc::test]] void test_forward_typedef_struct_operator(void) {
    TFwd a = {6};
    AssertEq((a + a).x, 12);
}

#pragma cccc suite end
