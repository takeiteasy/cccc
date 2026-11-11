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

@template(T, int N) struct Arr {
    T v[N];
};
@template(T, T N) T add_n(T x) {
    return x + N;
}
@template(int N) int signed_n(void) {
    return N;
}
@template(unsigned char N) int byte_n(void) {
    return N;
}
@template(bool B) int flag(void) {
    return B ? 1 : 2;
}
@template(int N) int pick(int i) {
    switch (i) {
        case N:
            return 1;
        default:
            return 0;
    }
}
@template(int N) int shadowed(void) {
    int N = 5;
    return N;
}
@template(unsigned long N) unsigned long big_n(void) {
    return N;
}
@template(T, int N) @equals struct Pad {
    T v[N];
};
@template(T, int N) struct Outer {
    @inst(Arr, T, N * 2) inner;
};

enum Color { RED, GREEN, BLUE, CRIMSON = RED };
enum Flags { FA = 1, FB = 2 };

@template(enum Color C) int shade(void) {
    return C;
}
@template(T, T N) int tagged(void) {
    return (int)N;
}

@template(T) typedef T *Ptr;
@template(T, int N) typedef T Row[N];
@template(R, A) typedef R (*Op)(A, A);
@template(T) typedef struct Link { T val; struct Link *next; } Link;
@template(T) typedef struct { T lo, hi; } Range;
@template(T) enum Level : T { LOW, MID = LOW + 2, HIGH = MID + 1 };
@template(int N) enum Cap { SMALL = N, LARGE = N * 2 };

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

[[cccc::test]]
void test_constant_parameter_sizes_array(void) {
    @inst(Arr, int, 16) a;
    AssertEq(sizeof(a), 16 * sizeof(int));
    AssertEq(sizeof(@inst(Arr, char, 3)), (size_t)3);
}

[[cccc::test]]
void test_constant_expressions_share_an_instance(void) {
    @inst(Arr, int, 16) a;
    struct Arr__int__16 *p = &a;
    @inst(Arr, int, 0x10) b;
    @inst(Arr, int, 8 * 2) c;
    AssertEq(sizeof(*p), sizeof(b));
    AssertEq(sizeof(b), sizeof(c));
    AssertEq((void *)&b.v[0] == (void *)&b, 1);
}

[[cccc::test]]
void test_constant_parameter_type_names_type_parameter(void) {
    AssertEq(@inst(add_n, long, 40)(2), 42);
    AssertEq(@inst(add_n, char, 2)(40), 42);
}

[[cccc::test]]
void test_negative_constant(void) {
    AssertEq(@inst(signed_n, -3)(), -3);
    AssertEq(signed_n__n3(), -3);
}

[[cccc::test]]
void test_constant_range_edges(void) {
    AssertEq(@inst(byte_n, 255)(), 255);
    AssertEq(@inst(byte_n, 0)(), 0);
    AssertEq(@inst(big_n, 18446744073709551615UL)(), 18446744073709551615UL);
    AssertEq(@inst(flag, 1)(), 1);
    AssertEq(@inst(flag, 0)(), 2);
}

[[cccc::test]]
void test_constant_in_case_label(void) {
    AssertEq(@inst(pick, 3)(3), 1);
    AssertEq(@inst(pick, 3)(4), 0);
}

[[cccc::test]]
void test_local_shadows_constant(void) {
    AssertEq(@inst(shadowed, 9)(), 5);
}

[[cccc::test]]
void test_nested_instance_uses_constant_expression(void) {
    @inst(Outer, short, 4) o;
    AssertEq(sizeof(o.inner), 8 * sizeof(short));
}

@inst(Pad, int, 2);

[[cccc::test]]
void test_derive_on_constant_template(void) {
    struct Pad__int__2 a = {{1, 2}}, b = {{1, 2}};
    AssertTrue(Pad__int__2_equals(&a, &b));
}

[[cccc::test]]
void test_enum_constant_named_by_enumerator(void) {
    AssertEq(@inst(shade, GREEN)(), 1);
    AssertEq(shade__GREEN(), 1);
    AssertEq(@inst(shade, BLUE)(), 2);
}

[[cccc::test]]
void test_aliased_enumerators_share_first_name(void) {
    AssertEq(@inst(shade, CRIMSON)(), 0);
    AssertEq(shade__RED(), 0);
    AssertEq(@inst(shade, RED)(), @inst(shade, CRIMSON)());
}

[[cccc::test]]
void test_enum_value_without_enumerator_uses_number(void) {
    AssertEq(@inst(shade, (enum Color)7)(), 7);
    AssertEq(shade__7(), 7);
}

[[cccc::test]]
void test_enum_through_type_parameter(void) {
    AssertEq(@inst(tagged, enum Flags, FB)(), 2);
    AssertEq(tagged__enum_Flags__FB(), 2);
    AssertEq(@inst(tagged, enum Flags, FA | FB)(), 3);
    AssertEq(tagged__enum_Flags__3(), 3);
}

[[cccc::test]]
void test_bool_constant_named_true_false(void) {
    AssertEq(flag__true(), 1);
    AssertEq(flag__false(), 2);
}

[[cccc::test]]
void test_typedef_template_is_a_named_typedef(void) {
    int x = 42;
    @inst(Ptr, int) p = &x;
    Ptr__int q        = p;
    AssertEq(*q, 42);
    AssertEq(sizeof(@inst(Ptr, char)), sizeof(char *));
    @inst(Ptr, long) lp = 0;
    Ptr__long        lq = lp;
    AssertEq(sizeof(lq), sizeof(long *));
}

[[cccc::test]]
void test_typedef_template_with_constant_parameter(void) {
    @inst(Row, int, 4) r = {1, 2, 3, 4};
    AssertEq(sizeof(r), 4 * sizeof(int));
    Row__int__4 *same = &r;
    AssertEq((*same)[3], 4);
}

static int add2(int a, int b) { return a + b; }
static int sub2(int a, int b) { return a - b; }

[[cccc::test]]
void test_typedef_template_function_pointer(void) {
    @inst(Op, int, int) f = add2;
    Op__int__int        g = sub2;
    AssertEq(f(40, 2), 42);
    AssertEq(g(44, 2), 42);
}

[[cccc::test]]
void test_typedef_template_of_tagged_struct(void) {
    @inst(Link, int) b = {2, 0};
    @inst(Link, int) a = {40, &b};
    struct Link__int *n = a.next;
    AssertEq(a.val + n->val, 42);
    @inst(Link, char) c = {1, 0};
    AssertEq(sizeof(c.val), (size_t)1);
}

[[cccc::test]]
void test_typedef_template_of_anonymous_struct(void) {
    @inst(Range, int)  r = {40, 42};
    @inst(Range, long) w = {1, 2};
    Range__long        v = w;
    AssertEq(v.lo, 1);
    AssertEq(r.hi, 42);
    AssertEq(w.hi, 2);
}

[[cccc::test]]
void test_enum_template_renames_enumerators(void) {
    @inst(Level, int) lv = Level__int__HIGH;
    AssertEq(lv, 3);
    AssertEq(Level__int__LOW, 0);
    AssertEq(Level__int__MID, 2);
    @inst(Level, char) small = Level__char__MID;
    AssertEq(sizeof(small), (size_t)1);
    AssertEq(sizeof(@inst(Level, long)), sizeof(long));
}

@inst(Cap, 4);
@inst(Cap, 10);

[[cccc::test]]
void test_enum_template_constant_parameter(void) {
    AssertEq(Cap__4__SMALL, 4);
    AssertEq(Cap__4__LARGE, 8);
    AssertEq(Cap__10__LARGE, 20);
}

@template(int N) int cap_of(void) {
    @inst(Cap, N) c = (@inst(Cap, N))N;
    return c;
}

[[cccc::test]]
void test_enum_template_inside_function_template(void) {
    AssertEq(@inst(cap_of, 21)(), 21);
}

@template(T, int N = 8) struct Dflt {
    T v[N];
};
@template(K, V = K) struct Same {
    K k;
    V v;
};
@template(T = int) struct Opt {
    T val;
};
@template(T, T N = 0) T plus_n(T x) {
    return x + N;
}
@template(T, int N = sizeof(T) * 4) struct Buf {
    T v[N];
};
enum Shade { RED_D, GREEN_D };
@template(enum Shade S = GREEN_D) int shade_of(void) {
    return S;
}

@inst(Dflt, char);

[[cccc::test]]
void test_default_constant_argument(void) {
    @inst(Dflt, int) a;
    AssertEq(sizeof(a), sizeof(int) * 8);
    struct Dflt__int__8 *p = &a;
    @inst(Dflt, int, 8) b;
    p = &b;
    AssertEq(sizeof(@inst(Dflt, int, 4)), sizeof(int) * 4);
    AssertEq(sizeof(struct Dflt__char__8), (size_t)8);
}

[[cccc::test]]
void test_default_type_argument(void) {
    @inst(Same, int) s       = {1, 2};
    struct Same__int__int *p = &s;
    AssertEq(p->k + p->v, 3);
    @inst(Same, int, char) t = {1, 2};
    AssertEq(sizeof(t.v), (size_t)1);
}

[[cccc::test]]
void test_default_for_every_parameter(void) {
    @inst(Opt) o       = {5};
    struct Opt__int *p = &o;
    AssertEq(p->val, 5);
    @inst(Opt, char) c = {1};
    AssertEq(sizeof(c.val), (size_t)1);
}

[[cccc::test]]
void test_default_names_earlier_parameter(void) {
    AssertEq(@inst(plus_n, long)(40), 40L);
    AssertEq(@inst(plus_n, long, 2)(40), 42L);
    AssertEq(sizeof(@inst(Buf, short)), sizeof(short) * sizeof(short) * 4);
    AssertEq(sizeof(@inst(Buf, int)), sizeof(int) * sizeof(int) * 4);
}

[[cccc::test]]
void test_default_enum_constant_is_named_by_enumerator(void) {
    AssertEq(@inst(shade_of)(), 1);
    AssertEq(shade_of__GREEN_D(), 1);
}
