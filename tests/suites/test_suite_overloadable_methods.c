// CCCC_FLAGS: --testing
// Suite: overloadable functions bound as methods and operators.

#define OVL __attribute__((overloadable))

typedef struct { int x, y; } Vec;

[[cccc::method(Vec, scale)]]
OVL int vec_scale(Vec *self, int k) { return self->x * k; }

[[cccc::method(Vec, scale)]]
OVL int vec_scale(Vec *self, double k) { return 100; }

[[cccc::method(Vec, scale)]]
OVL int vec_scale(Vec *self, Vec o) { return self->x * o.x; }

[[cccc::method(Vec)]]
OVL int len(Vec *self) { return 1; }

[[cccc::method(Vec)]]
OVL int len(Vec self, int extra) { return extra; }

[[cccc::method(Vec, hop)]]
OVL int hop_by(Vec *self, int by @default(5)) { return by; }

[[cccc::method(Vec, hop)]]
OVL int hop_by(Vec *self, const char *name) { return 7; }

[[cccc::static_method(Vec, make)]]
OVL Vec vec_make(int x) { return (Vec){x, 0}; }

[[cccc::static_method(Vec, make)]]
OVL Vec vec_make(int x, int y) { return (Vec){x, y}; }

[[cccc::test]]
void test_method_overloads_are_picked_by_argument(void) {
    Vec v = {3, 4};
    Vec *p = &v;
    Vec w = {5, 0};
    AssertEq(v.scale(2), 6);
    AssertEq(v.scale(2.0), 100);
    AssertEq(p->scale(w), 15);
    AssertEq(v.scale(2.0f), 100);
}

[[cccc::test]]
void test_receiver_by_pointer_or_value(void) {
    Vec v = {3, 4};
    Vec *p = &v;
    AssertEq(v.len(), 1);
    AssertEq(v.len(9), 9);
    AssertEq(p->len(8), 8);
}

[[cccc::test]]
void test_defaults_and_keys_in_a_method_set(void) {
    Vec v = {0, 0};
    AssertEq(v.hop(), 5);
    AssertEq(v.hop(2), 2);
    AssertEq(v.hop("a"), 7);
    AssertEq(v.hop(.by = 3), 3);
}

[[cccc::test]]
void test_static_method_set(void) {
    Vec a = Vec.make(3);
    Vec b = Vec.make(3, 4);
    AssertEq(a.x, 3);
    AssertEq(a.y, 0);
    AssertEq(b.x, 3);
    AssertEq(b.y, 4);
}

[[cccc::test]]
void test_type_call_over_an_instance_set(void) {
    Vec v = {3, 4};
    AssertEq(Vec.scale(&v, 2), 6);
    AssertEq(Vec.scale(&v, 2.0), 100);
}

// Operators.

[[cccc::operator(*)]]
OVL Vec vec_mul(Vec a, Vec b) { return (Vec){a.x * b.x, a.y * b.y}; }

[[cccc::operator(*)]]
OVL Vec vec_mul(Vec a, double k) { return (Vec){a.x * 10, a.y * 10}; }

[[cccc::operator(*)]]
OVL Vec vec_mul(Vec a, long k) { return (Vec){a.x * 20, a.y * 20}; }

[[cccc::operator(-)]]
OVL Vec vec_neg(Vec a) { return (Vec){-a.x, -a.y}; }

[[cccc::operator(-)]]
OVL Vec vec_sub(Vec a, Vec b) { return (Vec){a.x - b.x, a.y - b.y}; }

[[cccc::operator(+)]]
OVL Vec vec_add(const Vec *a, double k) { return (Vec){a->x + 1, a->y}; }

[[cccc::test]]
void test_operator_picked_by_ranking(void) {
    Vec v = {1, 2};
    Vec w = {3, 4};
    AssertEq((v * w).x, 3);
    AssertEq((v * 2.5).x, 10);
    AssertEq((v * 2L).x, 20);
    AssertEq((v * 2.5f).x, 10);
}

[[cccc::test]]
void test_unary_and_binary_minus_coexist(void) {
    Vec v = {1, 2};
    Vec w = {5, 9};
    AssertEq((-v).x, -1);
    AssertEq((w - v).y, 7);
}

[[cccc::test]]
void test_ranked_operator_takes_operand_by_address(void) {
    Vec v = {1, 2};
    AssertEq((v + 2).x, 2);
}

// Exact before ranked, across the fallbacks.

typedef struct { int n; } Num;

[[cccc::operator(==)]]
OVL bool num_eq(Num a, Num b) { return a.n == b.n; }

[[cccc::operator(==)]]
OVL bool num_eq(Num a, long b) { return a.n == b; }

[[cccc::operator(<=>)]]
OVL int num_cmp(Num a, int b) { return 1000; }

[[cccc::operator(<)]]
OVL bool num_lt(Num a, double b) { return true; }

[[cccc::test]]
void test_not_equal_falls_back_to_overloadable_equality(void) {
    Num a = {3};
    Num b = {4};
    AssertEq(a != b, true);
    AssertEq(a != 3, false);
    AssertEq(a == 3, true);
}

[[cccc::test]]
void test_exact_threeway_beats_ranked_relational(void) {
    Num a = {3};
    AssertEq(a < 2, false);       // num_cmp(a, 2) < 0, not num_lt by conversion
    AssertEq(a > 2, true);
    AssertEq(a < 2.0, true);      // num_lt(a, 2.0) is exact
}

// Subscript.

typedef struct { int data[4]; } Buf;

[[cccc::operator([])]]
OVL int *buf_at(Buf *b, int i) { return &b->data[i]; }

[[cccc::operator([])]]
OVL int *buf_at(Buf *b, const char *name) { return &b->data[3]; }

[[cccc::test]]
void test_subscript_picks_by_index_type(void) {
    Buf b = {{1, 2, 3, 4}};
    AssertEq(b[1], 2);
    AssertEq(b["last"], 4);
    b["last"] = 40;
    AssertEq(b.data[3], 40);
    short s = 0;
    AssertEq(b[s], 1);
}

// Written in the struct body.

typedef struct Acc {
    int total;
    @overloadable int add(Acc *self, int k) { return self->total + k; }
    @overloadable int add(Acc *self, Acc o) { return self->total + o.total; }
    @overloadable Acc operator+(Acc a, int k) { return (Acc){a.total + k}; }
    @overloadable Acc operator+(Acc a, Acc b) { return (Acc){a.total + b.total}; }
} Acc;

[[cccc::test]]
void test_overloadable_methods_in_a_struct_body(void) {
    Acc a = {10};
    Acc b = {5};
    AssertEq(a.add(1), 11);
    AssertEq(a.add(b), 15);
}

[[cccc::test]]
void test_overloadable_operators_in_a_struct_body(void) {
    Acc a = {10};
    Acc b = {5};
    AssertEq((a + 1).total, 11);
    AssertEq((a + b).total, 15);
    AssertEq((a + 1.0).total, 11);   // double converts to int, ranked
}
