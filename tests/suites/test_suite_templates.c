// CCCC_FLAGS: --testing
// Suite: @template definitions and @inst instantiation.

#include <cccc/derive.h>
#include <stddef.h>

@template(T) struct Vec { T *data; size_t len, cap; };
@template(T) T max(T a, T b) { return a > b ? a : b; }
@template(A, B) struct Pair { A first; B second; };
@template(T) struct Node { T val; struct Node *next; };
@template(T) T fact(T n) { return n <= 1 ? 1 : n * @inst(fact, T)(n - 1); }
@template(T) struct Box { @inst(Pair, T, int) inner; };
@template(T) static inline T twice(T x) { return x + x; }
@template(T) @equals struct Cell { T a; T b; };
@template(T) union Bits { T value; unsigned char raw[sizeof(T)]; };
@template(T) T first_of(T *p) { T v = *p; return v; }

struct Point { int x, y; };

[[cccc::test]]
void test_struct_instance(void) {
    @inst(Vec, int) v = {0};
    int x = 7;
    v.data = &x;
    v.len  = 1;
    AssertEq(v.data[0], 7);
    AssertEq(v.len, (size_t)1);
}

[[cccc::test]]
void test_function_instance(void) {
    AssertEq(@inst(max, int)(40, 42), 42);
    AssertEq(@inst(max, long)(5, 3), 5);
}

[[cccc::test]]
void test_instances_are_distinct_types(void) {
    @inst(Vec, int) a;
    @inst(Vec, char) b;
    AssertEq(sizeof(*a.data), sizeof(int));
    AssertEq(sizeof(*b.data), sizeof(char));
}

[[cccc::test]]
void test_mangled_names_are_usable(void) {
    @inst(Vec, int) v;
    struct Vec__int w = {0};
    AssertEq(sizeof(v), sizeof(w));
    AssertEq(max__int(1, 9), 9);
}

[[cccc::test]]
void test_typedef_mangles_to_canonical_type(void) {
    @inst(Vec, size_t) a;
    struct Vec__unsigned_long b;
    AssertEq(sizeof(a), sizeof(b));
}

[[cccc::test]]
void test_pointer_argument(void) {
    @inst(Vec, int *) v = {0};
    struct Vec__int_ptr w;
    AssertEq(sizeof(v), sizeof(w));
    AssertEq(sizeof(*v.data), sizeof(int *));
}

[[cccc::test]]
void test_const_argument(void) {
    @inst(Vec, const char *) v = {0};
    struct Vec__const_char_ptr w;
    AssertEq(sizeof(v), sizeof(w));
}

[[cccc::test]]
void test_struct_argument(void) {
    struct Point pt = {1, 2};
    @inst(Pair, struct Point, struct Point) pp = {pt, pt};
    struct Pair__struct_Point__struct_Point q;
    AssertEq(pp.second.y, 2);
    AssertEq(sizeof(pp), sizeof(q));
}

[[cccc::test]]
void test_two_parameters(void) {
    @inst(Pair, int, float) p = {3, 1.5f};
    struct Pair__int__float q;
    AssertEq(p.first, 3);
    AssertEq(sizeof(p), sizeof(q));
}

[[cccc::test]]
void test_self_reference(void) {
    @inst(Node, int) b = {2, 0};
    @inst(Node, int) a = {1, &b};
    AssertEq(a.next->val, 2);
}

[[cccc::test]]
void test_recursive_function_template(void) {
    AssertEq(@inst(fact, int)(5), 120);
    AssertEq(@inst(fact, long)(6), 720);
}

[[cccc::test]]
void test_instance_inside_a_template(void) {
    @inst(Box, size_t) b = {{7, 8}};
    AssertEq(b.inner.second, 8);
}

[[cccc::test]]
void test_same_instance_from_two_sites(void) {
    @inst(Vec, int) a;
    @inst(Vec, int) b;
    a.len = 3;
    b.len = a.len;
    AssertEq(b.len, (size_t)3);
    AssertEq(@inst(max, int)(1, 2), max__int(1, 2));
}

[[cccc::test]]
void test_instance_in_sizeof_cast_and_compound_literal(void) {
    AssertEq(sizeof(@inst(Pair, int, int)), 2 * sizeof(int));
    int x = (int)@inst(max, int)(3, 4);
    AssertEq(x, 4);
    @inst(Pair, int, int) p = (@inst(Pair, int, int)){5, 6};
    AssertEq(p.second, 6);
}

static int takes_vec(@inst(Vec, int) *v) {
    return (int)v->len;
}

[[cccc::test]]
void test_instance_in_parameter_list(void) {
    @inst(Vec, int) v = {0, 4, 0};
    AssertEq(takes_vec(&v), 4);
}

@inst(Vec, long) global_vec = {0, 9, 0};

[[cccc::test]]
void test_instance_at_file_scope(void) {
    AssertEq(global_vec.len, (size_t)9);
}

[[cccc::test]]
void test_static_inline_template(void) {
    AssertEq(@inst(twice, int)(21), 42);
}

[[cccc::test]]
void test_derive_runs_on_the_instance(void) {
    @inst(Cell, int) a = {1, 2};
    @inst(Cell, int) b = {1, 2};
    @inst(Cell, int) c = {1, 3};
    AssertTrue(Cell__int_equals(&a, &b));
    AssertFalse(Cell__int_equals(&a, &c));
}

[[cccc::test]]
void test_union_template(void) {
    @inst(Bits, int) u;
    u.value = 0;
    AssertEq(sizeof(u), sizeof(int));
}

[[cccc::test]]
void test_pointer_parameter_in_function_template(void) {
    int x = 42;
    AssertEq(@inst(first_of, int)(&x), 42);
}

// A file-scope `@inst(...);` instantiates without declaring anything to use.
@inst(Cell, long);
@inst(twice, long);

[[cccc::test]]
void test_forced_instance_exists(void) {
    struct Cell__long a = {1, 2}, b = {1, 2};
    AssertTrue(Cell__long_equals(&a, &b));
    AssertEq(twice__long(21), 42);
}

typedef @inst(Vec, short) ShortVec;
#define twice_char @inst(twice, char)

[[cccc::test]]
void test_typedef_and_macro_aliases(void) {
    ShortVec v = {0, 3, 0};
    AssertEq(v.len, (size_t)3);
    AssertEq(twice_char(21), 42);
}
