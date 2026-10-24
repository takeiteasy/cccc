// CCCC_FLAGS: --testing
// Suite: an operator written in a struct body is T_operator_<name>.
#pragma cccc suite begin "operators_struct"

typedef struct Vec2 {
    int  x, y;
    Vec2 operator + (Vec2 a, Vec2 b) {
        return (Vec2){a.x + b.x, a.y + b.y};
    }
    Vec2 operator - (Vec2 a, Vec2 b) {
        return (Vec2){a.x - b.x, a.y - b.y};
    }
    Vec2 operator *(Vec2 v, int k) {
        return (Vec2){v.x * k, v.y * k};
    }
    bool        operator == (const Vec2 *a, const Vec2 *b);
    bool        operator < (Vec2 a, Vec2 b);
    static bool operator != (const Vec2 *a, const Vec2 *b) {
        return a->x != b->x || a->y != b->y;
    }
    inline bool operator > (Vec2 a, Vec2 b) {
        return a.x > b.x;
    }
    int len(Vec2 *self) {
        return self->x + self->y;
    }
} Vec2;

bool Vec2_operator_eq(const Vec2 *a, const Vec2 *b) {
    return a->x == b->x && a->y == b->y;
}

bool Vec2_operator_lt(Vec2 a, Vec2 b) {
    return a.x < b.x;
}

// clang-format off
struct Ordered {
    int v;
    int operator<=>(const struct Ordered *a, const struct Ordered *b) {
        return a->v - b->v;
    }
};
// clang-format on

[[cccc::test]] void test_in_struct_three_way(void) {
    struct Ordered a = {1}, b = {2};
    AssertEq(!!(a < b), 1);
    AssertEq(!!(a >= b), 0);
    AssertEq(Ordered_operator_cmp(&a, &b) < 0, 1);
}

struct Tagged {
    int           v;
    struct Tagged operator + (struct Tagged a, struct Tagged b) {
        return (struct Tagged){a.v + b.v};
    }
};

union Pun {
    int       i;
    union Pun operator + (union Pun a, union Pun b) {
        return (union Pun){a.i + b.i};
    }
};

typedef struct Named {
    int operator;
    int (*operator_cb)(int);
    int operator_len(struct Named *self) {
        return self->operator;
    }
} Named;

typedef struct Method {
    int k;
    int operator(Method *self) {
        return self->k;
    }
} Method;

[[cccc::test(return = 42)]]
int test_struct_operator_arithmetic(void) {
    Vec2 a = {1, 2}, b = {3, 4};
    Vec2 c = a + b;
    if (c.x != 4 || c.y != 6)
        return 1;
    c = c - a;
    if (c.x != 3 || c.y != 4)
        return 2;
    c = a * 3;
    if (c.x != 3 || c.y != 6)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_struct_operator_comparison(void) {
    Vec2 a = {1, 2}, b = {3, 4};
    if (!(a == a) || a == b)
        return 1;
    if (!(a != b) || a != a)
        return 2;
    if (!(a < b) || b < a)
        return 3;
    if (!(b > a) || a > b)
        return 4;
    return 42;
}

[[cccc::test(return = 42)]]
int test_struct_operator_compound(void) {
    Vec2 a = {1, 2}, b = {3, 4};
    a += b;
    if (a.x != 4 || a.y != 6)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_struct_operator_by_name_and_method(void) {
    Vec2 a = {1, 2}, b = {3, 4};
    Vec2 c = Vec2_operator_add(a, b);
    if (c.x != 4 || c.len() != 10)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_struct_operator_tag_and_union(void) {
    struct Tagged a = {3}, b = {4};
    struct Tagged t = a + b;
    union Pun     u = {2};
    union Pun     w = u + u;
    if (t.v != 7 || w.i != 4)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_struct_operator_name_stays_member(void) {
    Named  n = {5, 0};
    Method m = {6};
    if (n.operator != 5 || n.operator_len() != 5)
        return 1;
    if (m.operator() != 6 || Method_operator(&m) != 6)
        return 2;
    return 42;
}

#pragma cccc suite end
