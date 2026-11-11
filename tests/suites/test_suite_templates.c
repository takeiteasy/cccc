// CCCC_FLAGS: --testing
// Suite: @template definitions and @inst instantiation.

#include <cccc/derive.h>
#include <stddef.h>

@template(T) struct Vec { T *data; size_t len, cap; };
@template(T) T max(T a, T b) { return a > b ? a : b; }
@template(A, B) struct Pair { A first; B second; };
@template(T) struct Node { T val; struct Node *next; };
@template(T) T fact(T n) {
    return n <= 1 ? 1 : n * @inst(T) fact(n - 1);
}
@template(T) struct Box {
    @inst(T, int) Pair inner;
};
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
    @inst(T, N * 2) Arr inner;
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
    @inst(int) Vec v = {0};
    int x = 7;
    v.data = &x;
    v.len  = 1;
    AssertEq(v.data[0], 7);
    AssertEq(v.len, (size_t)1);
}

[[cccc::test]]
void test_function_instance(void) {
    AssertEq(@inst(int) max(40, 42), 42);
    AssertEq(@inst(long) max(5, 3), 5);
}

[[cccc::test]]
void test_instances_are_distinct_types(void) {
    @inst(int) Vec  a;
    @inst(char) Vec b;
    AssertEq(sizeof(*a.data), sizeof(int));
    AssertEq(sizeof(*b.data), sizeof(char));
}

[[cccc::test]]
void test_mangled_names_are_usable(void) {
    @inst(int) Vec  v;
    struct Vec__int w = {0};
    AssertEq(sizeof(v), sizeof(w));
    AssertEq(max__int(1, 9), 9);
}

[[cccc::test]]
void test_typedef_mangles_to_canonical_type(void) {
    @inst(size_t) Vec         a;
    struct Vec__unsigned_long b;
    AssertEq(sizeof(a), sizeof(b));
}

[[cccc::test]]
void test_pointer_argument(void) {
    @inst(int *) Vec    v = {0};
    struct Vec__int_ptr w;
    AssertEq(sizeof(v), sizeof(w));
    AssertEq(sizeof(*v.data), sizeof(int *));
}

[[cccc::test]]
void test_const_argument(void) {
    @inst(const char *) Vec    v = {0};
    struct Vec__const_char_ptr w;
    AssertEq(sizeof(v), sizeof(w));
}

[[cccc::test]]
void test_struct_argument(void) {
    struct Point pt = {1, 2};
    @inst(struct Point, struct Point) Pair  pp = {pt, pt};
    struct Pair__struct_Point__struct_Point q;
    AssertEq(pp.second.y, 2);
    AssertEq(sizeof(pp), sizeof(q));
}

[[cccc::test]]
void test_two_parameters(void) {
    @inst(int, float) Pair  p = {3, 1.5f};
    struct Pair__int__float q;
    AssertEq(p.first, 3);
    AssertEq(sizeof(p), sizeof(q));
}

[[cccc::test]]
void test_self_reference(void) {
    @inst(int) Node b = {2, 0};
    @inst(int) Node a = {1, &b};
    AssertEq(a.next->val, 2);
}

[[cccc::test]]
void test_recursive_function_template(void) {
    AssertEq(@inst(int) fact(5), 120);
    AssertEq(@inst(long) fact(6), 720);
}

[[cccc::test]]
void test_instance_inside_a_template(void) {
    @inst(size_t) Box b = {{7, 8}};
    AssertEq(b.inner.second, 8);
}

[[cccc::test]]
void test_same_instance_from_two_sites(void) {
    @inst(int) Vec a;
    @inst(int) Vec b;
    a.len = 3;
    b.len = a.len;
    AssertEq(b.len, (size_t)3);
    AssertEq(@inst(int) max(1, 2), max__int(1, 2));
}

[[cccc::test]]
void test_instance_in_sizeof_cast_and_compound_literal(void) {
    AssertEq(sizeof(@inst(int, int) Pair), 2 * sizeof(int));
    int x = (int)@inst(int) max(3, 4);
    AssertEq(x, 4);
    @inst(int, int) Pair p = (@inst(int, int) Pair){5, 6};
    AssertEq(p.second, 6);
}

static int takes_vec(@inst(int) Vec *v) {
    return (int)v->len;
}

[[cccc::test]]
void test_instance_in_parameter_list(void) {
    @inst(int) Vec v = {0, 4, 0};
    AssertEq(takes_vec(&v), 4);
}

@inst(long) Vec global_vec = {0, 9, 0};

[[cccc::test]]
void test_instance_at_file_scope(void) {
    AssertEq(global_vec.len, (size_t)9);
}

[[cccc::test]]
void test_static_inline_template(void) {
    AssertEq(@inst(int) twice(21), 42);
}

[[cccc::test]]
void test_derive_runs_on_the_instance(void) {
    @inst(int) Cell a = {1, 2};
    @inst(int) Cell b = {1, 2};
    @inst(int) Cell c = {1, 3};
    AssertTrue(Cell__int_equals(&a, &b));
    AssertFalse(Cell__int_equals(&a, &c));
}

[[cccc::test]]
void test_union_template(void) {
    @inst(int) Bits u;
    u.value = 0;
    AssertEq(sizeof(u), sizeof(int));
}

[[cccc::test]]
void test_pointer_parameter_in_function_template(void) {
    int x = 42;
    AssertEq(@inst(int) first_of(&x), 42);
}

// A file-scope `@inst ...;` instantiates without declaring anything to use.
@inst(long) Cell;
@inst(long) twice;

[[cccc::test]]
void test_forced_instance_exists(void) {
    struct Cell__long a = {1, 2}, b = {1, 2};
    AssertTrue(Cell__long_equals(&a, &b));
    AssertEq(twice__long(21), 42);
}

typedef @inst(short) Vec ShortVec;
#define twice_char @inst(char) twice

[[cccc::test]]
void test_typedef_and_macro_aliases(void) {
    ShortVec v = {0, 3, 0};
    AssertEq(v.len, (size_t)3);
    AssertEq(twice_char(21), 42);
}

[[cccc::test]]
void test_constant_parameter_sizes_array(void) {
    @inst(int, 16) Arr a;
    AssertEq(sizeof(a), 16 * sizeof(int));
    AssertEq(sizeof(@inst(char, 3) Arr), (size_t)3);
}

[[cccc::test]]
void test_constant_expressions_share_an_instance(void) {
    @inst(int, 16) Arr    a;
    struct Arr__int__16 *p = &a;
    @inst(int, 0x10) Arr  b;
    @inst(int, 8 * 2) Arr c;
    AssertEq(sizeof(*p), sizeof(b));
    AssertEq(sizeof(b), sizeof(c));
    AssertEq((void *)&b.v[0] == (void *)&b, 1);
}

[[cccc::test]]
void test_constant_parameter_type_names_type_parameter(void) {
    AssertEq(@inst(long, 40) add_n(2), 42);
    AssertEq(@inst(char, 2) add_n(40), 42);
}

[[cccc::test]]
void test_negative_constant(void) {
    AssertEq(@inst(-3) signed_n(), -3);
    AssertEq(signed_n__n3(), -3);
}

[[cccc::test]]
void test_constant_range_edges(void) {
    AssertEq(@inst(255) byte_n(), 255);
    AssertEq(@inst(0) byte_n(), 0);
    AssertEq(@inst(18446744073709551615UL) big_n(), 18446744073709551615UL);
    AssertEq(@inst(1) flag(), 1);
    AssertEq(@inst(0) flag(), 2);
}

[[cccc::test]]
void test_constant_in_case_label(void) {
    AssertEq(@inst(3) pick(3), 1);
    AssertEq(@inst(3) pick(4), 0);
}

[[cccc::test]]
void test_local_shadows_constant(void) {
    AssertEq(@inst(9) shadowed(), 5);
}

[[cccc::test]]
void test_nested_instance_uses_constant_expression(void) {
    @inst(short, 4) Outer o;
    AssertEq(sizeof(o.inner), 8 * sizeof(short));
}

@inst(int, 2) Pad;

[[cccc::test]]
void test_derive_on_constant_template(void) {
    struct Pad__int__2 a = {{1, 2}}, b = {{1, 2}};
    AssertTrue(Pad__int__2_equals(&a, &b));
}

[[cccc::test]]
void test_enum_constant_named_by_enumerator(void) {
    AssertEq(@inst(GREEN) shade(), 1);
    AssertEq(shade__GREEN(), 1);
    AssertEq(@inst(BLUE) shade(), 2);
}

[[cccc::test]]
void test_aliased_enumerators_share_first_name(void) {
    AssertEq(@inst(CRIMSON) shade(), 0);
    AssertEq(shade__RED(), 0);
    AssertEq(@inst(RED) shade(), @inst(CRIMSON) shade());
}

[[cccc::test]]
void test_enum_value_without_enumerator_uses_number(void) {
    AssertEq(@inst((enum Color)7) shade(), 7);
    AssertEq(shade__7(), 7);
}

[[cccc::test]]
void test_enum_through_type_parameter(void) {
    AssertEq(@inst(enum Flags, FB) tagged(), 2);
    AssertEq(tagged__enum_Flags__FB(), 2);
    AssertEq(@inst(enum Flags, FA | FB) tagged(), 3);
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
    @inst(int) Ptr p        = &x;
    Ptr__int q        = p;
    AssertEq(*q, 42);
    AssertEq(sizeof(@inst(char) Ptr), sizeof(char *));
    @inst(long) Ptr  lp = 0;
    Ptr__long        lq = lp;
    AssertEq(sizeof(lq), sizeof(long *));
}

[[cccc::test]]
void test_typedef_template_with_constant_parameter(void) {
    @inst(int, 4) Row r = {1, 2, 3, 4};
    AssertEq(sizeof(r), 4 * sizeof(int));
    Row__int__4 *same = &r;
    AssertEq((*same)[3], 4);
}

static int add2(int a, int b) { return a + b; }
static int sub2(int a, int b) { return a - b; }

[[cccc::test]]
void test_typedef_template_function_pointer(void) {
    @inst(int, int) Op  f = add2;
    Op__int__int        g = sub2;
    AssertEq(f(40, 2), 42);
    AssertEq(g(44, 2), 42);
}

[[cccc::test]]
void test_typedef_template_of_tagged_struct(void) {
    @inst(int) Link   b = {2, 0};
    @inst(int) Link   a = {40, &b};
    struct Link__int *n = a.next;
    AssertEq(a.val + n->val, 42);
    @inst(char) Link c = {1, 0};
    AssertEq(sizeof(c.val), (size_t)1);
}

[[cccc::test]]
void test_typedef_template_of_anonymous_struct(void) {
    @inst(int) Range   r = {40, 42};
    @inst(long) Range  w = {1, 2};
    Range__long        v = w;
    AssertEq(v.lo, 1);
    AssertEq(r.hi, 42);
    AssertEq(w.hi, 2);
}

[[cccc::test]]
void test_enum_template_renames_enumerators(void) {
    @inst(int) Level lv = Level__int__HIGH;
    AssertEq(lv, 3);
    AssertEq(Level__int__LOW, 0);
    AssertEq(Level__int__MID, 2);
    @inst(char) Level small = Level__char__MID;
    AssertEq(sizeof(small), (size_t)1);
    AssertEq(sizeof(@inst(long) Level), sizeof(long));
}

@inst(4) Cap;
@inst(10) Cap;

[[cccc::test]]
void test_enum_template_constant_parameter(void) {
    AssertEq(Cap__4__SMALL, 4);
    AssertEq(Cap__4__LARGE, 8);
    AssertEq(Cap__10__LARGE, 20);
}

@template(int N) int cap_of(void) {
    @inst(N) Cap c = (@inst(N) Cap)N;
    return c;
}

[[cccc::test]]
void test_enum_template_inside_function_template(void) {
    AssertEq(@inst(21) cap_of(), 21);
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

@inst(char) Dflt;

[[cccc::test]]
void test_default_constant_argument(void) {
    @inst(int) Dflt a;
    AssertEq(sizeof(a), sizeof(int) * 8);
    struct Dflt__int__8 *p = &a;
    @inst(int, 8) Dflt   b;
    p = &b;
    AssertEq(sizeof(@inst(int, 4) Dflt), sizeof(int) * 4);
    AssertEq(sizeof(struct Dflt__char__8), (size_t)8);
}

[[cccc::test]]
void test_default_type_argument(void) {
    @inst(int) Same        s = {1, 2};
    struct Same__int__int *p = &s;
    AssertEq(p->k + p->v, 3);
    @inst(int, char) Same t = {1, 2};
    AssertEq(sizeof(t.v), (size_t)1);
}

[[cccc::test]]
void test_default_for_every_parameter(void) {
    @inst Opt        o = {5};
    struct Opt__int *p = &o;
    AssertEq(p->val, 5);
    @inst(char) Opt c = {1};
    AssertEq(sizeof(c.val), (size_t)1);
}

[[cccc::test]]
void test_default_names_earlier_parameter(void) {
    AssertEq(@inst(long) plus_n(40), 40L);
    AssertEq(@inst(long, 2) plus_n(40), 42L);
    AssertEq(sizeof(@inst(short) Buf), sizeof(short) * sizeof(short) * 4);
    AssertEq(sizeof(@inst(int) Buf), sizeof(int) * sizeof(int) * 4);
}

[[cccc::test]]
void test_default_enum_constant_is_named_by_enumerator(void) {
    AssertEq(@inst shade_of(), 1);
    AssertEq(shade_of__GREEN_D(), 1);
}

#define VEC_INT       Vec @(int)
#define max_long      max @(long)
#define PAIR_OF(A, B) @inst(A, B) Pair

[[cccc::test]]
void test_postfix_spelling(void) {
    Vec @(int) v       = {0};
    struct Vec__int *p = &v;
    AssertEq(p->len, (size_t)0);
    AssertEq(max @(int)(40, 42), 42);
    AssertEq(sizeof(Pair @(int, long)), sizeof(@inst(int, long) Pair));
    AssertEq(sizeof(Arr @(int, 4)), 4 * sizeof(int));
}

[[cccc::test]]
void test_postfix_nests(void) {
    Pair @(int, Vec @(char)) p            = {1, {0}};
    struct Pair__int__struct_Vec__char *q = &p;
    AssertEq(q->first, 1);
    @inst(Vec @(char), int) Pair r = {{0}, 2};
    AssertEq(r.second, 2);
}

[[cccc::test]]
void test_empty_postfix_takes_defaults(void) {
    Opt @() o          = {5};
    struct Opt__int *p = &o;
    AssertEq(p->val, 5);
    AssertEq(shade_of @()(), 1);
}

[[cccc::test]]
void test_spellings_in_macros(void) {
    VEC_INT          v = {0};
    struct Vec__int *p = &v;
    AssertEq(p->len, (size_t)0);
    AssertEq(max_long(5, 3), 5L);
    PAIR_OF(int, char) q = {1, 2};
    AssertEq(q.second, 2);
}

// ---- float and pointer constant parameters ----

@template(double D) double scale(double x) {
    _Static_assert(D == 1.5, "D folds");
    return x * D;
}
@template(double D) struct Gain {
    double v;
};
@template(float F) float fsrc(void) {
    return F;
}
@template(T, T V) T constant(void) {
    return V;
}
@template(double D, double E = D * 2) double pair_sum(void) {
    return D + E;
}
@template(double D = 1.0) double dflt(void) {
    return D;
}
@template(double D) int array_len(void) {
    int a[(int)D];
    return (int)sizeof a / (int)sizeof a[0];
}
@template(double D) const double *addr_of(void) {
    return &D;
}
@template(double D) double hidden(void) {
    double D = 9;
    return D;
}

@template(T, int (*CMP)(T, T)) int compare(T a, T b) {
    static int (*saved)(T, T) = CMP;
    if (CMP == NULL)
        return -1000;
    return CMP(a, b) + (saved == CMP ? 1000 : 0);
}
@template(int (*F)(int) = NULL) int apply(int x) {
    return F ? F(x) : x;
}
@template(const int *P) int deref(void) {
    return *P;
}
@template(int (*F)(int)) int through(int x) {
    return @inst(F) apply(x);
}

static int cmp_int(int a, int b) {
    return a - b;
}
static int twice(int x) {
    return 2 * x;
}
static int ptr_global = 7;
static int ptr_table[3] = {4, 5, 6};

[[cccc::test]]
void test_float_argument_spellings_share_an_instance(void) {
    AssertEq(@inst(1.5) scale(2.0), 3.0);
    AssertEq(@inst(3.0 / 2) scale(2.0), 3.0);
    AssertTrue(&scale__1p5 == &scale__1p5);
    AssertEq(sizeof(@inst(1.5) Gain), sizeof(struct Gain__1p5));
}

[[cccc::test]]
void test_float_names(void) {
    AssertEq(sizeof(@inst(0.25) Gain), sizeof(struct Gain__0p25));
    AssertEq(sizeof(@inst(-0.25) Gain), sizeof(struct Gain__n0p25));
    AssertEq(sizeof(@inst(1e-5) Gain), sizeof(struct Gain__1em05));
    AssertEq(sizeof(@inst(1e20) Gain), sizeof(struct Gain__1e20));
    AssertEq(sizeof(@inst(100.0) Gain), sizeof(struct Gain__100));
    AssertEq(sizeof(@inst(0.0) Gain), sizeof(struct Gain__0));
    AssertEq(sizeof(@inst(-0.0) Gain), sizeof(struct Gain__n0));
    AssertEq(sizeof(@inst(__builtin_inf()) Gain), sizeof(struct Gain__inf));
    AssertEq(sizeof(@inst(-__builtin_inf()) Gain), sizeof(struct Gain__ninf));
}

[[cccc::test]]
void test_float_parameter_is_named_by_its_own_type(void) {
    AssertEq(@inst(0.1) fsrc(), 0.1f);
    AssertTrue(&fsrc__0p1 == &fsrc__0p1);
}

[[cccc::test]]
void test_float_argument_converts_from_integer(void) {
    AssertEq(@inst(3) dflt(), 3.0);
    AssertTrue(&dflt__3 == &dflt__3);
}

[[cccc::test]]
void test_constant_takes_an_earlier_type(void) {
    AssertEq(@inst(double, 2.5) constant(), 2.5);
    AssertEq(@inst(float, 0.5) constant(), 0.5f);
    AssertEq(@inst(int, 7) constant(), 7);
}

[[cccc::test]]
void test_float_defaults(void) {
    AssertEq(@inst(2.0) pair_sum(), 6.0);
    AssertEq(@inst(2.0, 1.0) pair_sum(), 3.0);
    AssertEq(@inst() dflt(), 1.0);
    AssertEq(@inst(4.0) dflt(), 4.0);
}

[[cccc::test]]
void test_float_parameter_in_constant_expressions(void) {
    AssertEq(@inst(3.0) array_len(), 3);
    static double folded = 2 * 1.5;
    AssertEq(folded, 3.0);
}

[[cccc::test]]
void test_float_parameter_has_an_address_and_is_const(void) {
    const double *p = @inst(2.5) addr_of();
    AssertEq(*p, 2.5);
    AssertTrue(p == @inst(2.5) addr_of());
    AssertTrue(p != @inst(3.5) addr_of());
}

[[cccc::test]]
void test_local_hides_float_parameter(void) {
    AssertEq(@inst(2.0) hidden(), 9.0);
}

[[cccc::test]]
void test_function_pointer_parameter(void) {
    AssertEq(@inst(int, cmp_int) compare(5, 3), 1002);
    AssertEq(@inst(int, &cmp_int) compare(5, 3), 1002);
    AssertTrue(&compare__int__cmp_int == &compare__int__cmp_int);
}

[[cccc::test]]
void test_null_function_pointer_parameter(void) {
    AssertEq(@inst(int, NULL) compare(5, 3), -1000);
    AssertEq(@inst(int, 0) compare(5, 3), -1000);
    AssertEq(@inst(int, nullptr) compare(5, 3), -1000);
    AssertEq(@inst(int, (int (*)(int, int))0) compare(5, 3), -1000);
    AssertTrue(&compare__int__null == &compare__int__null);
}

[[cccc::test]]
void test_function_pointer_default(void) {
    AssertEq(@inst() apply(4), 4);
    AssertEq(@inst(NULL) apply(4), 4);
    AssertEq(@inst(twice) apply(4), 8);
}

[[cccc::test]]
void test_pointer_parameter_forwards_to_another_template(void) {
    AssertEq(@inst(twice) through(4), 8);
}

[[cccc::test]]
void test_object_pointer_parameter(void) {
    AssertEq(@inst(&ptr_global) deref(), 7);
    AssertEq(@inst(ptr_table) deref(), 4);
}
