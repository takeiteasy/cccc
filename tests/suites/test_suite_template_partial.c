// CCCC_FLAGS: --testing
// Suite: a partial specialization matches a pattern of template arguments.

#include <cccc/derive.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

@template(T) struct Vec {
    T     *data;
    size_t len;
};
@template(T) @specialize(T *) struct Vec {
    T    **slots;
    size_t n;
    size_t extra;
};
@template(T) @specialize(const T *) struct Vec {
    const T **ro;
};

@template(T, int N = 8) struct Arr {
    T v[N];
};
@template(T) @specialize(T, 4) struct Arr {
    T a, b, c, d;
};
@template(int N) @specialize(char, N) struct Arr {
    char text[N + 1];
};

@template(A, B) struct Two {
    A a;
    B b;
};
@template(T) @specialize(T, T) struct Two {
    T both[2];
};
@template(T) @specialize(T, T *) struct Two {
    T  value;
    T *ptr;
};

@template(T) struct Box {
    T v;
};
@template(T) @specialize(T[4]) struct Box {
    T   four[4];
    int tag;
};
@template(R, A) @specialize(R (*)(A)) struct Box {
    R (*fn)(A);
    int call;
};

@template(T) T pick(T a, T b) {
    return a > b ? a : b;
}
@template(T) @specialize(T *) T *pick(T *a, T *b) {
    return a;
}

@template(T) enum Level : T { LOW, HIGH = 5 };
@template(T) @specialize(T *) enum Level : int { PTR_LOW, PTR_HIGH = 9 };

@template(T) typedef T Alias;
@template(T) @specialize(T *) typedef long Alias;

@template(T) @equals struct Cell {
    T a;
    T b;
};
@template(T) @equals @specialize(T *) struct Cell {
    T  *a;
    T  *b;
    int extra;
};

[[cccc::test]]
void test_pointer_pattern(void) {
    @inst(int *) Vec p = {0};
    p.extra            = 5;
    @inst(int) Vec v   = {0, 3};
    AssertEq(p.extra, 5);
    AssertEq(v.len, 3);
    AssertTrue(sizeof(p) == 3 * sizeof(void *));
}

[[cccc::test]]
void test_most_specialised_pattern_wins(void) {
    @inst(const char *) Vec c = {0};
    AssertTrue(sizeof(c) == sizeof(void *));
    @inst(char *) Vec n = {0};
    AssertTrue(sizeof(n) == 3 * sizeof(void *));
}

[[cccc::test]]
void test_constant_pattern(void) {
    @inst(int, 4) Arr a = {1, 2, 3, 4};
    @inst(int) Arr    b;
    AssertEq(a.d, 4);
    AssertEq(sizeof(b.v), 8 * sizeof(int));
}

[[cccc::test]]
void test_constant_variable_binds_the_argument(void) {
    @inst(char, 5) Arr s;
    AssertEq(sizeof(s.text), 6);
    strcpy(s.text, "hello");
    AssertEq(strlen(s.text), 5);
}

[[cccc::test]]
void test_repeated_variable(void) {
    @inst(int, int) Two   same;
    @inst(int, long) Two  mixed = {1, 2};
    @inst(int, int *) Two ptr;
    AssertEq(sizeof(same.both), 2 * sizeof(int));
    AssertEq(mixed.b, 2);
    AssertTrue(sizeof(ptr.ptr) == sizeof(int *));
}

[[cccc::test]]
void test_array_and_function_pointer_patterns(void) {
    @inst(short[4]) Box      arr;
    @inst(int (*)(char)) Box fn;
    @inst(int) Box           plain = {7};
    arr.tag                        = 1;
    fn.call                        = 2;
    AssertEq(sizeof(arr.four), 4 * sizeof(short));
    AssertEq(fn.call, 2);
    AssertEq(plain.v, 7);
}

[[cccc::test]]
void test_function_partial(void) {
    int  x = 1, y = 2;
    int *r = @inst(int *) pick(&x, &y);
    AssertTrue(r == &x);
    AssertEq(@inst(int) pick(3, 9), 9);
}

[[cccc::test]]
void test_enum_and_typedef_partials(void) {
    @inst(char) Level   a = Level__char__HIGH;
    @inst(char *) Level b = Level__char_ptr__PTR_HIGH;
    @inst(int) Alias    c = 1;
    @inst(int *) Alias  d = 1;
    AssertEq(a, 5);
    AssertEq(b, 9);
    AssertEq(sizeof(c), sizeof(int));
    AssertEq(sizeof(d), sizeof(long));
}

[[cccc::test]]
void test_partial_carries_its_own_attributes(void) {
    @inst(int) Cell   x = {1, 2}, y = {1, 2};
    int               z = 1;
    @inst(int *) Cell p = {&z, &z, 0}, q = {&z, &z, 0};
    AssertTrue(Cell__int_equals(&x, &y));
    AssertTrue(Cell__int_ptr_equals(&p, &q));
}

[[cccc::test]]
void test_instance_names_ignore_the_partial(void) {
    @inst(int *) Vec     a;
    struct Vec__int_ptr *b = &a;
    AssertTrue(b == &a);
}

// Float and pointer constants bind partial specialization variables.
@template(double A, double B) struct FPair {
    char kind;
};
@template(double D) @specialize(D, D) struct FPair {
    char   kind[2];
    double v[(int)D];
};
@template(float F) @specialize(F, 1.0) struct FPair {
    char kind[3];
};

@template(double A, double B) double fsum(void) {
    return A + B;
}
@template(double D) @specialize(D, D) double fsum(void) {
    return D * 100;
}

[[cccc::test]]
void test_float_variable_binds_repeated_value(void) {
    @inst(2.0, 2.0) FPair a;
    @inst(2.0, 3.0) FPair b;
    AssertEq(sizeof(a.kind), 2);
    AssertEq(sizeof(a.v), 2 * sizeof(double));
    AssertEq(sizeof(b.kind), 1);
}

[[cccc::test]]
void test_float_variable_must_represent_the_value(void) {
    @inst(0.5, 1.0) FPair exact;
    @inst(0.1, 1.0) FPair inexact;
    AssertEq(sizeof(exact.kind), 3);
    AssertEq(sizeof(inexact.kind), 1);
}

[[cccc::test]]
void test_float_variable_is_readable_in_the_body(void) {
    AssertEq(fsum @(1.5, 1.5)(), 150.0);
    AssertEq(fsum @(1.5, 2.5)(), 4.0);
}

static int tp_a, tp_b;
static int tp_fn(void) {
    return 1;
}

// Variables of different types are different sets: the narrower type is the
// more specialised partial.
@template(double A, double B) struct Wid {
    char kind[1];
};
@template(float F) @specialize(F, 1.0) struct Wid {
    char kind[2];
};
@template(double D) @specialize(D, 1.0) struct Wid {
    char kind[3];
};

@template(long A, long B) struct IWid {
    char kind[1];
};
@template(char N) @specialize(N, N) struct IWid {
    char kind[2];
};
@template(long N) @specialize(N, N) struct IWid {
    char kind[3];
};

@template(T, long N) struct LArr {
    char kind[1];
};
@template(T, unsigned char N) @specialize(T[N], N) struct LArr {
    char kind[2];
};
@template(T, long N) @specialize(T[N], N) struct LArr {
    char kind[3];
};

@template(void *P, void *Q) struct PWid {
    char kind[1];
};
@template(int *P) @specialize(P, P) struct PWid {
    char kind[2];
};
@template(void *P) @specialize(P, P) struct PWid {
    char kind[3];
};

[[cccc::test]]
void test_float_variable_is_narrower_than_double(void) {
    @inst(0.5, 1.0) Wid exact;
    @inst(0.1, 1.0) Wid inexact;
    AssertEq(sizeof(exact.kind), 2);
    AssertEq(sizeof(inexact.kind), 3);
}

[[cccc::test]]
void test_narrow_integer_variable_is_more_specialised(void) {
    @inst(5, 5) IWid       small;
    @inst(1000, 1000) IWid big;
    @inst(5, 6) IWid       apart;
    AssertEq(sizeof(small.kind), 2);
    AssertEq(sizeof(big.kind), 3);
    AssertEq(sizeof(apart.kind), 1);
}

[[cccc::test]]
void test_object_pointer_variable_is_narrower_than_void_pointer(void) {
    @inst(&tp_a, &tp_a) PWid same;
    AssertEq(sizeof(same.kind), 2);
}

@template(int *P, int *Q) struct PPair {
    char kind;
};
@template(int *P) @specialize(P, P) struct PPair {
    char kind[2];
};

@template(int *P, int *Q) int pread(void) {
    return 0;
}
@template(int *P) @specialize(P, P) int pread(void) {
    return *P + 10;
}

@template(int (*F)(void), int (*G)(void)) int pcall(void) {
    return 0;
}
@template(int (*F)(void)) @specialize(F, F) int pcall(void) {
    return F() + 20;
}

[[cccc::test]]
void test_pointer_variable_binds_repeated_object(void) {
    @inst(&tp_a, &tp_a) PPair a;
    @inst(&tp_a, &tp_b) PPair b;
    @inst(0, 0) PPair c;
    @inst(&tp_a, 0) PPair d;
    AssertEq(sizeof(a.kind), 2);
    AssertEq(sizeof(b.kind), 1);
    AssertEq(sizeof(c.kind), 2);
    AssertEq(sizeof(d.kind), 1);
}

[[cccc::test]]
void test_pointer_variable_is_readable_in_the_body(void) {
    tp_a = 5;
    AssertEq(pread @(&tp_a, &tp_a)(), 15);
    AssertEq(pread @(&tp_a, &tp_b)(), 0);
}

[[cccc::test]]
void test_function_pointer_variable(void) {
    AssertEq(pcall @(tp_fn, tp_fn)(), 21);
    AssertEq(pcall @(tp_fn, 0)(), 0);
}
