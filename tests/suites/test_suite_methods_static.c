// CCCC_FLAGS: --testing
// Suite: static methods, called through the type as T.f(...).
#pragma cccc suite begin "methods_static"

// clang-format off
typedef struct Vec2 {
    int x, y;
    static Vec2 make(int x, int y) {
        return (Vec2){x, y};
    }
    static int sum(Vec2 *v) {
        return v->x + v->y;
    }
    static inline int twice(int a) {
        return a * 2;
    }
    int len(Vec2 *self) {
        return self->x + self->y;
    }
    static Vec2 origin(void);
    static int defaulted(int a, int b @default(10)) {
        return a + b;
    }
} Vec2;

Vec2 Vec2_origin(void) {
    return (Vec2){0, 0};
}

struct Tagged {
    int v;
    static struct Tagged of(int v) {
        return (struct Tagged){v};
    }
};

union Pun {
    int i;
    static int zero(void) {
        return 0;
    }
};

typedef struct Plain {
    int a;
} Plain;

[[cccc::static_method(Plain, make)]]
Plain plain_make(int a) {
    return (Plain){a};
}

[[cccc::static_method(Plain)]]
Plain plain_zero(void) {
    return (Plain){0};
}

[[cccc::static_method(Plain, defaulted)]]
int plain_defaulted(int a, int b @default(10)) {
    return a + b;
}

[[cccc::method(Plain, get)]]
int plain_get(Plain *p) {
    return p->a;
}

[[cccc::static_method(Plain, internal)]]
static int plain_internal(void) {
    return 7;
}

@template(T)
struct Box {
    T v;
    static struct Box wrap(T v) {
        return (struct Box){v};
    }
    T get(struct Box *self) {
        return self->v;
    }
};

[[cccc::test(return = 42)]]
int test_in_struct_static_method(void) {
    Vec2 a = Vec2.make(3, 4);
    if (a.x != 3 || a.y != 4)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_static_method_taking_the_struct(void) {
    Vec2 a = {3, 4};
    if (Vec2.sum(&a) != 7)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_static_inline_method(void) {
    if (Vec2.twice(21) != 42)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_static_method_declared_then_defined(void) {
    Vec2 o = Vec2.origin();
    return o.x + o.y + 42;
}

[[cccc::test(return = 42)]]
int test_static_method_is_callable_by_name(void) {
    Vec2 a = Vec2_make(1, 2);
    int (*f)(int, int) = plain_defaulted;
    if (a.y != 2 || f(1, 2) != 3)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_static_method_defaults_and_keys(void) {
    if (Plain.defaulted(1) != 11)
        return 1;
    if (Plain.defaulted(1, .b = 2) != 3)
        return 2;
    if (Vec2.defaulted(1) != 11 || Vec2.defaulted(1, .b = 2) != 3)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_instance_method_through_the_type(void) {
    Vec2 a = {3, 4};
    if (Vec2.len(&a) != 7 || a.len() != 7)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_tagged_struct_and_union(void) {
    struct Tagged t = struct Tagged.of(5);
    if (t.v != 5 || union Pun.zero() != 0)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_attribute_form(void) {
    Plain a = Plain.make(5);
    Plain z = Plain.plain_zero();
    if (a.get() != 5 || z.a != 0 || Plain.internal() != 7)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_template_instance(void) {
    Box@(int) b = Box@(int).wrap(9);
    if (b.v != 9 || b.get() != 9)
        return 1;
    struct Box_int c = Box@(int).wrap(4);
    if (Box@(int).get(&c) != 4)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_static_call_where_a_type_could_start(void) {
    Vec2 a = Vec2.make(1, 2), b = Vec2.make(3, 4);
    Vec2.make(5, 6);
    int n = sizeof(Vec2.make(1, 2)) == sizeof(Vec2) ? 1 : 0;
    int m = (int)Vec2.twice(1);
    int k = Vec2.make(7, 8).y;
    if (a.y + b.x + n + m + k != 2 + 3 + 1 + 2 + 8)
        return 1;
    return 42;
}
// clang-format on

#pragma cccc suite end
