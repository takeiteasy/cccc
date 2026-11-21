// CCCC_FLAGS: --testing
// Suite: a function template that shares its name with an overload set is
// ranked with the set's functions; a tie goes to the function.

#define OVL __attribute__((overloadable))

// A function that matches exactly beats the template, which wins over a
// conversion.
static int OVL f(int x) {
    return 1;
}
static int OVL f(long x) {
    return 2;
}
@template(T) static int f(T x) {
    return 10;
}

[[cccc::test]]
void test_member_ties_template(void) {
    AssertEq(f(1), 1);
    AssertEq(f(1L), 2);
}

[[cccc::test]]
void test_template_beats_conversion(void) {
    AssertEq(f(2.0), 10);
    AssertEq(f(1.5f), 10);
    AssertEq(f((short)1), 10);
}

[[cccc::test]]
void test_template_takes_pointer_and_struct(void) {
    int a = 0;
    struct P {
        int v;
    } p = {0};
    AssertEq(f(&a), 10);
    AssertEq(f(p), 10);
    AssertEq(f("text"), 10);
}

[[cccc::test]]
void test_explicit_instance_skips_set(void) {
    AssertEq(f<int>(1), 10);
    AssertEq(@inst(int) f(1), 10);
    AssertEq(f<double>(1), 10);
}

// The template first, then the functions.
@template(T) static int g(T x) {
    return 10;
}
static int OVL g(int x) {
    return 1;
}
static int OVL g(double x) {
    return 2;
}

[[cccc::test]]
void test_template_declared_first(void) {
    AssertEq(g(1), 1);
    AssertEq(g(1.0), 2);
    AssertEq(g(1L), 10);
    AssertEq(g("s"), 10);
}

// A template whose arguments do not deduce is no candidate.
@template(T) static int h(T *p) {
    return 10;
}
static int OVL h(int x) {
    return 1;
}
static int OVL h(double x) {
    return 2;
}

[[cccc::test]]
void test_failed_deduction_falls_back_to_set(void) {
    int a = 0;
    AssertEq(h(3), 1);
    AssertEq(h(3.0), 2);
    AssertEq(h(&a), 10);
}

// A parameter the template names concretely takes conversions.
@template(T) static int m(T a, int b) {
    return 10;
}
static int OVL m(double a, double b) {
    return 1;
}

[[cccc::test]]
void test_concrete_template_parameter(void) {
    AssertEq(m(1, 2), 10);
    AssertEq(m(1.0, 2), 10);
    AssertEq(m(1.0, 2.0), 1);
}

// A qualified parameter pattern.
@template(T) static int q(const T *p) {
    return 10;
}
static int OVL q(const int *p) {
    return 1;
}
static int OVL q(long x) {
    return 2;
}

[[cccc::test]]
void test_qualified_pattern(void) {
    int       a = 0;
    const int b = 0;
    double    d = 0;
    AssertEq(q(&a), 1);
    AssertEq(q(&b), 1);
    AssertEq(q(&d), 10);
    AssertEq(q(3L), 2);
}

// A parameter pack.
@template(Ts...) static int count(Ts... xs) {
    return 100 + sizeof...(Ts);
}
static int OVL count(int a) {
    return 1;
}
static int OVL count(int a, int b) {
    return 2;
}

[[cccc::test]]
void test_pack_template(void) {
    AssertEq(count(1), 1);
    AssertEq(count(1, 2), 2);
    AssertEq(count(1, 2, 3), 103);
    AssertEq(count(1.0), 101);
    AssertEq(count(1, 2.0), 102);
    AssertEq(count(), 100);
}

// The unmarked function of the name keeps its plain name.
static int plain(int x) {
    return 1;
}
static int OVL plain(long x) {
    return 2;
}
@template(T) static int plain(T x) {
    return 10;
}

[[cccc::test]]
void test_unmarked_function_with_template(void) {
    AssertEq(plain(1), 1);
    AssertEq(plain(1L), 2);
    AssertEq(plain(1.0), 10);
}

// A bare name stands for the set's functions only.
[[cccc::test]]
void test_bare_name_picks_set_member(void) {
    int (*p)(int)  = f;
    int (*q)(long) = &f;
    AssertEq(p(1), 1);
    AssertEq(q(1), 2);
}

// A template that returns a deduced value still takes the call's arguments.
@template(T) static T twice(T x) {
    return x + x;
}
static int OVL twice(int x) {
    return x * 2 + 1;
}

[[cccc::test]]
void test_instance_returns_deduced_type(void) {
    AssertEq(twice(4), 9);
    AssertEq(twice(1.5), 3.0);
    AssertEq(twice(2.0f), 4.0f);
}

// Taking the address of the set while the template is a candidate of calls.
static int OVL apply(int (*fn)(int), int v) {
    return fn(v);
}
@template(T) static int apply(T fn, int v) {
    return 10;
}

static int OVL inc(int x) {
    return x + 1;
}
static int OVL inc(double x) {
    return 0;
}

[[cccc::test]]
void test_call_argument_naming_a_set(void) {
    AssertEq(apply(inc, 4), 5);
}

// An instance with an empty pack does not take the name of the unmarked
// function, and a later call still ranks the set.
@template(Ts...) static int bare(Ts... xs) {
    return 100 + sizeof...(Ts);
}
static int bare(int a) {
    return 1;
}
static int OVL bare(long a) {
    return 2;
}

[[cccc::test]]
void test_empty_pack_instance_beside_unmarked_function(void) {
    AssertEq(bare(), 100);
    AssertEq(bare(5), 1);
    AssertEq(bare(5L), 2);
    AssertEq(bare(5.0), 101);
}
