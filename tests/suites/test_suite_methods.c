// CCCC_FLAGS: --testing
// Suite: [[cccc::method(T)]] makes a free function callable as s.f(x).
#pragma cccc suite begin "methods"

typedef struct {
    int x, y;
} Vec2;

typedef struct {
    int x, y, z;
} Vec3;

typedef struct {
    int (*len)(int);
} Handler;

[[cccc::method(Vec2, len)]]
int vec2_len(Vec2 *self) {
    return self->x + self->y;
}

[[cccc::method(Vec3, len)]]
int vec3_len(const Vec3 *self) {
    return self->x + self->y + self->z;
}

[[cccc::method(Vec2)]]
int dot(Vec2 a, Vec2 b) {
    return a.x * b.x + a.y * b.y;
}

[[cccc::method(Vec2)]]
void scale(Vec2 *self, long k) {
    self->x *= k;
    self->y *= k;
}

[[cccc::method(Handler)]]
int len(Handler *self, int a) {
    return 1000;
}

static Vec2 make_vec2(int x, int y) {
    return (Vec2){x, y};
}

static int twice(int a) {
    return a * 2;
}

[[cccc::test(return = 42)]]
int test_method_on_value_and_pointer(void) {
    Vec2  v  = {1, 2};
    Vec2 *pv = &v;
    if (v.len() != 3)
        return 1;
    if (pv->len() != 3)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_method_name_per_type(void) {
    Vec2 v = {1, 2};
    Vec3 w = {1, 2, 3};
    const Vec3 cw = {4, 5, 6};
    if (v.len() != 3 || w.len() != 6 || cw.len() != 15)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_method_by_value_receiver(void) {
    Vec2  v  = {1, 2};
    Vec2 *pv = &v;
    if (v.dot((Vec2){3, 4}) != 11)
        return 1;
    if (pv->dot(v) != 5)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_method_mutates_through_pointer_and_converts_arguments(void) {
    Vec2  v  = {1, 2};
    Vec2 *pv = &v;
    v.scale(3);
    if (v.x != 3 || v.y != 6)
        return 1;
    pv->scale(2);
    if (v.x != 6 || v.y != 12)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_method_rvalue_receiver(void) {
    if (make_vec2(5, 6).len() != 11)
        return 1;
    make_vec2(1, 1).scale(9);
    return 42;
}

[[cccc::test(return = 42)]]
int test_method_in_expressions(void) {
    Vec2 v = {1, 2};
    if (v.len() + v.len() != 6)
        return 1;
    if (make_vec2(1, 1).dot(v) + (&v)->len() != 6)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_member_function_pointer_wins(void) {
    Handler h = {twice};
    if (h.len(4) != 8)
        return 1;
    return 42;
}

[[cccc::method(Vec2, ilen)]]
static inline int vec2_ilen(Vec2 *self) {
    return self->x + self->y + 1;
}

[[cccc::test(return = 42)]]
int test_method_static_inline(void) {
    Vec2 v = {1, 2};
    if (v.ilen() != 4)
        return 1;
    return 42;
}

#pragma cccc suite end
