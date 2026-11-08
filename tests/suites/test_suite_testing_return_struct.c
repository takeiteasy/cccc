// CCCC_FLAGS: --testing
// Suite: struct and union return= assertions compare field by field, in the VM
// and under --testing=native.

#include <stddef.h>

#pragma cccc suite begin "return_struct"

struct In {
    int  p;
    char c;
};

struct N {
    int           a;
    struct In     in;
    int           arr[4];
    char          name[6];
    const char   *s;
    struct N     *next;
    float         f;
    double        d;
    unsigned char u;
};

union U {
    int   i;
    float f;
};

struct W {
    union U u;
    struct {
        int ax;
        int ay;
    };
    long      big[64];
    char      buf[4096];
    struct In ins[2];
};

struct Bits {
    int      a : 3;
    unsigned b : 5;
    int      c;
};

struct Wide {
    long double x;
    int         y;
};

static struct N                                        other;

[[cccc::test(return = (struct N){.a    = 1,
                                 .in   = {.p = 2, .c = 'x'},
                                 .arr  = {1, 2, 3, 4},
                                 .name = "hey",
                                 .s    = "str",
                                 .next = NULL,
                                 .f    = 1.5,
                                 .d    = 2.5,
                                 .u = 200})]] struct N test_every_field(void) {
    return (struct N){1,    {2, 'x'}, {1, 2, 3, 4}, "hey", "str",
                      NULL, 1.5f,     2.5,          200};
}

[[cccc::test(return = (struct N){.a = 1})]] struct N
    test_unlisted_fields_are_zero(void) {
    struct N n = {1};
    return n;
}

[[cccc::test(return = (struct N){.arr = {1, 2}})]] struct N
    test_array_tail_is_zero(void) {
    struct N n = {0};
    n.arr[0]   = 1;
    n.arr[1]   = 2;
    return n;
}

[[cccc::test(return = (struct N){.next != NULL})]] struct N
    test_pointer_field_non_null(void) {
    struct N n = {0};
    n.next     = &other;
    return n;
}

[[cccc::test(return = (struct N){.s != NULL})]] struct N
    test_string_field_non_null(void) {
    struct N n = {0};
    n.s        = "x";
    return n;
}

[[cccc::test(return != (struct N){.a = 9})]] struct N
    test_whole_value_differs(void) {
    struct N n = {1};
    return n;
}

[[cccc::test(return = (struct N){.a != 9})]] struct N test_field_differs(void) {
    struct N n = {1};
    return n;
}

[[cccc::test(return = (union U){.i = 5})]] union U test_union_arm(void) {
    union U u;
    u.i = 5;
    return u;
}

[[cccc::test(return = (struct W){.u   = {.i = 7},
                                 .ax  = 3,
                                 .ay  = 4,
                                 .ins = {{.p = 1, .c = 'a'},
                                         {.p = 2, .c = 'b'}}})]] struct W
    test_union_anonymous_and_nested_arrays(void) {
    struct W w = {0};
    w.u.i      = 7;
    w.ax       = 3;
    w.ay       = 4;
    w.ins[0]   = (struct In){1, 'a'};
    w.ins[1]   = (struct In){2, 'b'};
    return w;
}

[[cccc::test(return = (struct Bits){.a = -2, .b = 9, .c = 7})]] struct Bits
    test_bit_fields(void) {
    return (struct Bits){-2, 9, 7};
}

[[cccc::test(return = (struct Wide){.x = 1.5, .y = 3})]] struct Wide
    test_long_double_field(void) {
    return (struct Wide){1.5L, 3};
}

typedef struct {
    int x, y;
} Pt;

typedef union {
    int   i;
    float f;
} Num;

typedef struct {
    Pt tl;
    Pt pts[2];
} Box;

struct Q {
    int v;
};

struct Pair {
    Pt        a;
    struct Q  b;
};

[[cccc::test(return = (Pt){.x = 3, .y = 4})]] Pt test_typedef_literal(void) {
    return (Pt){3, 4};
}

[[cccc::test(return != (Pt){.x = 3, .y = 5})]] Pt
    test_typedef_literal_ne(void) {
    return (Pt){3, 4};
}

[[cccc::test(return = (Num){.i = 7})]] Num test_typedef_union(void) {
    Num n;
    n.i = 7;
    return n;
}

[[cccc::test(return = (Box){.tl  = (Pt){.x = 1, .y = 2},
                            .pts = {(Pt){.x = 3, .y = 4},
                                    {.x = 5, .y = 6}}})]] Box
    test_typedef_nested(void) {
    return (Box){{1, 2}, {{3, 4}, {5, 6}}};
}

[[cccc::test(return = (struct W){.u   = (union U){.i = 7},
                                 .ax  = 3,
                                 .ins = {(struct In){.p = 1, .c = 'a'},
                                         {.p = 2, .c = 'b'}}})]] struct W
    test_tag_literals_nested(void) {
    struct W w = {0};
    w.u.i      = 7;
    w.ax       = 3;
    w.ins[0]   = (struct In){1, 'a'};
    w.ins[1]   = (struct In){2, 'b'};
    return w;
}

[[cccc::test(return = (struct Pair){.a = (Pt){.x = 1, .y = 2},
                                    .b = (struct Q){.v = 9}})]] struct Pair
    test_typedef_and_tag_mixed(void) {
    return (struct Pair){{1, 2}, {9}};
}

[[cccc::test(return = (const Pt){.x = 3, .y = 4})]] Pt
    test_const_typedef_literal(void) {
    return (Pt){3, 4};
}

[[cccc::test(return = (Pt const){.x = 3, .y = 4})]] Pt
    test_trailing_const_literal(void) {
    return (Pt){3, 4};
}

[[cccc::test(return =
                        (const struct Pair){
                            .a = (const Pt){.x = 1, .y = 2},
                            .b = (volatile struct Q){.v = 9}})]] struct Pair
    test_qualified_nested_literals(void) {
    return (struct Pair){{1, 2}, {9}};
}

[[cccc::test(return = (_Atomic Pt){.x = 3, .y = 4})]] Pt
    test_atomic_typedef_literal(void) {
    return (Pt){3, 4};
}

[[cccc::test(return = (Pt _Atomic){.x = 3, .y = 4})]] Pt
    test_trailing_atomic_literal(void) {
    return (Pt){3, 4};
}

[[cccc::test(return = (struct Pair){.a = (_Atomic Pt){.x = 1, .y = 2},
                                    .b = {.v = 9}})]] struct Pair
    test_atomic_nested_literal(void) {
    return (struct Pair){{1, 2}, {9}};
}

[[cccc::test(return = (static Pt){.x = 3, .y = 4})]] Pt
    test_static_literal(void) {
    return (Pt){3, 4};
}

[[cccc::test(return = (constexpr Pt){.x = 3, .y = 4})]] Pt
    test_constexpr_literal(void) {
    return (Pt){3, 4};
}

[[cccc::test(return = (register Pt){.x = 3, .y = 4})]] Pt
    test_register_literal(void) {
    return (Pt){3, 4};
}

[[cccc::test(return = (thread_local Pt){.x = 3, .y = 4})]] Pt
    test_thread_local_literal(void) {
    return (Pt){3, 4};
}

[[cccc::test(return = (static struct Pair){.a = (constexpr Pt){.x = 1, .y = 2},
                                           .b = {.v = 9}})]] struct Pair
    test_storage_class_nested_literal(void) {
    return (struct Pair){{1, 2}, {9}};
}

#pragma cccc suite end
