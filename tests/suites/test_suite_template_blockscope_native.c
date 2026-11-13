// CCCC_FLAGS: --testing
// Suite: a block-scope struct, union or enum as a template argument, through
// pointers, nesting and function instances.

#include <stddef.h>

@template(T) struct Vec {
    T     *data;
    size_t len, cap;
};
@template(A, B) struct Pair {
    A first;
    B second;
};
@template(T) static int first_short(T *p) {
    return p->x[0];
}
@template(T) static T *identity(T *p) {
    T *copy = p;
    return copy;
}

struct Inner {
    int a, b;
};
struct Clash {
    char c;
};
static int Green = 5;

static int block_scope_pointer_and_nesting(void) {
    struct Ambient {
        short x[3];
    };
    @inst(struct Ambient, int) Pair                  inner = {{{1, 2, 3}}, 4};
    @inst(@inst(struct Ambient, int) Pair, int) Pair outer = {inner, 5};
    struct Ambient                                  *pa    = &inner.first;
    @inst(struct Ambient *) Vec                      v     = {&pa, 1};
    return (int)sizeof outer.first.first + v.data[0]->x[2] + outer.second;
}

[[cccc::test]]
void test_block_scope_aggregates_in_pointers_and_nesting(void) {
    AssertEq(block_scope_pointer_and_nesting(), 6 + 3 + 5);
}

static int function_instance_signature(void) {
    struct Ambient {
        short x[3];
    };
    struct Ambient  a    = {{7, 8, 9}};
    struct Ambient *same = @inst(struct Ambient) identity(&a);
    return @inst(struct Ambient) first_short(same);
}

[[cccc::test]]
void test_function_instance_over_block_scope_struct(void) {
    AssertEq(function_instance_signature(), 7);
}

static int union_and_enum_arguments(void) {
    union Bits {
        int   i;
        short s[2];
    };
    enum Color { Red = 3, Green = 4 };
    union Bits                  u  = {.i = 0};
    enum Color                  c  = Green;
    enum Color                 *cp = &c;
    @inst(union Bits) Vec       uv = {&u, 1};
    @inst(enum Color *) Vec     cv = {&cp, 1};
    @inst(enum Color, int) Pair p  = {Red, 6};
    return (int)sizeof uv.data[0].s + (int)*cv.data[0] + p.first + p.second;
}

[[cccc::test]]
void test_union_and_enum_block_scope_arguments(void) {
    AssertEq(union_and_enum_arguments(), 4 + 4 + 3 + 6);
    AssertEq(Green, 5);
}

static int file_scope_member(void) {
    struct Outer {
        struct Inner in;
        int          k;
    };
    struct Outer            o = {{1, 2}, 3};
    @inst(struct Outer) Vec v = {&o, 1};
    return v.data[0].in.b + v.data[0].k;
}

[[cccc::test]]
void test_block_scope_struct_with_file_scope_member(void) {
    AssertEq(file_scope_member(), 2 + 3);
}

static int clash_small(void) {
    struct Clash {
        short s;
    };
    struct Clash            c = {4};
    @inst(struct Clash) Vec v = {&c, 1};
    return v.data[0].s + (int)sizeof v.data[0];
}

static int clash_large(void) {
    struct Clash {
        long l[2];
    };
    struct Clash            c = {{5, 6}};
    @inst(struct Clash) Vec v = {&c, 1};
    return (int)v.data[0].l[1] + (int)sizeof v.data[0];
}

[[cccc::test]]
void test_block_scope_types_that_clash_with_file_scope(void) {
    struct Clash            file = {'x'};
    @inst(struct Clash) Vec v    = {&file, 1};
    AssertEq(clash_small(), 4 + 2);
    AssertEq(clash_large(), 6 + 16);
    AssertEq(v.data[0].c, 'x');
}

static int nested_block_pointer(void) {
    struct Named {
        int x;
    };
    struct Named outer = {1};
    int          sum   = outer.x;
    {
        struct Named {
            char c[3];
        };
        struct Named            inner  = {{1, 2, 3}};
        @inst(struct Named) Vec v      = {&inner, 1};
        sum                           += (int)sizeof v.data[0];
    }
    return sum;
}

[[cccc::test]]
void test_nested_block_same_tag_through_pointer(void) {
    AssertEq(nested_block_pointer(), 1 + 3);
}
