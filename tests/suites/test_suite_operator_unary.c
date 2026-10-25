// CCCC_FLAGS: --testing
// Suite: a one-parameter [[cccc::operator(...)]] binds - + ~ ! for struct
// operands, and ++ -- use the T + int / T - int functions.
#pragma cccc suite begin "operator_unary"

typedef struct {
    int x, y;
} V;

[[cccc::operator(-)]]
V v_neg(V a) {
    return (V){-a.x, -a.y};
}

[[cccc::operator(-)]]
V v_sub(V a, V b) {
    return (V){a.x - b.x, a.y - b.y};
}

[[cccc::operator(-)]]
V v_sub_int(V a, int n) {
    return (V){a.x - n, a.y - n};
}

[[cccc::operator(+)]]
V v_pos(const V *a) {
    return *a;
}

[[cccc::operator(+)]]
V v_add(V a, V b) {
    return (V){a.x + b.x, a.y + b.y};
}

[[cccc::operator(+)]]
V v_add_int(V a, int n) {
    return (V){a.x + n, a.y + n};
}

[[cccc::operator(~)]]
V v_compl(V a) {
    return (V){~a.x, ~a.y};
}

[[cccc::operator(!)]]
bool v_not(const V *a) {
    return a->x == 0 && a->y == 0;
}

[[cccc::test]] void test_unary_minus_by_value_and_initializer(void) {
    V v = {1, 2};
    V w = -v;
    AssertEq(w.x, -1);
    AssertEq(w.y, -2);
    w = -w;
    AssertEq(w.x, 1);
}

[[cccc::test]] void test_unary_and_binary_minus_coexist(void) {
    V a = {5, 7}, b = {1, 2};
    V d = a - b;
    AssertEq(d.x, 4);
    V n = -(a - b);
    AssertEq(n.y, -5);
    V k = a - 1;
    AssertEq(k.x, 4);
}

[[cccc::test]] void test_unary_plus_by_address(void) {
    V v = {3, 4};
    V p = +v;
    AssertEq(p.y, 4);
    V q = +(v + v);
    AssertEq(q.x, 6);
}

[[cccc::test]] void test_complement(void) {
    V v = {0, 1};
    V c = ~v;
    AssertEq(c.x, -1);
    AssertEq(c.y, ~1);
}

[[cccc::test]] void test_not_returns_scalar(void) {
    V zero = {0, 0}, one = {0, 1};
    AssertEq(!zero, 1);
    AssertEq(!one, 0);
    AssertEq(!!one, 1);
}

[[cccc::test]] void test_prefix_increment_and_decrement(void) {
    V v = {1, 1};
    V r = ++v;
    AssertEq(r.x, 2);
    AssertEq(v.x, 2);
    r = --v;
    AssertEq(r.x, 1);
    --v;
    AssertEq(v.x, 0);
}

[[cccc::test]] void test_postfix_yields_old_value(void) {
    V v = {1, 2};
    V old = v++;
    AssertEq(old.x, 1);
    AssertEq(v.x, 2);
    AssertEq(v.y, 3);
    old = v--;
    AssertEq(old.x, 2);
    AssertEq(v.x, 1);
}

[[cccc::test]] void test_postfix_evaluates_operand_once(void) {
    V arr[2] = {{1, 1}, {5, 5}};
    int i = 0;
    arr[i++]++;
    AssertEq(i, 1);
    AssertEq(arr[0].x, 2);
    AssertEq(arr[1].x, 5);
}

typedef struct W {
    int n;
    W operator-(W a) {
        return (W){-a.n};
    }
    W operator-(W a, W b) {
        return (W){a.n - b.n};
    }
    W operator~(W a) {
        return (W){~a.n};
    }
    bool operator!(const W *a) {
        return a->n == 0;
    }
} W;

[[cccc::test]] void test_in_struct_unary_operators(void) {
    W w = {5}, z = {0};
    AssertEq((-w).n, -5);
    AssertEq((w - w).n, 0);
    AssertEq((~w).n, ~5);
    AssertEq(!z, 1);
    AssertEq(!w, 0);
    AssertEq(W_operator_neg(w).n, -5);
    AssertEq(W_operator_sub(w, w).n, 0);
}

#pragma cccc suite end
