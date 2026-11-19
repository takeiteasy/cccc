// CCCC_FLAGS: --testing
// Suite: a bare function template name converted to a function pointer
// deduces the template from the pointer type.

#define OVL __attribute__((overloadable))

@template(T) static T twice(T x) {
    return x + x;
}
@template(T) static T zero(void) {
    return 0;
}
@template(T, U) static U second(T a, U b) {
    return b;
}
@template(Ts...) static int count(Ts... xs) {
    return sizeof...(Ts);
}

static int via(int (*fn)(int), int x) {
    return fn(x);
}
static int (*gp)(int) = twice;

[[cccc::test]]
void test_initializer(void) {
    double (*p)(double) = twice;
    int (*q)(int)       = twice;
    AssertEq((int)p(21.0), 42);
    AssertEq(q(21), 42);
}

[[cccc::test]]
void test_assignment_and_cast(void) {
    long (*p)(long);
    p = twice;
    AssertEq(p(4), 8);
    AssertEq(((short (*)(short))twice)(4), 8);
}

[[cccc::test]]
void test_argument_and_return(void) {
    AssertEq(via(twice, 21), 42);
}

static int (*pick(void))(int) {
    return twice;
}

[[cccc::test]]
void test_return_statement(void) {
    AssertEq(pick()(5), 10);
}

[[cccc::test]]
void test_file_scope_and_static_initializer(void) {
    static int (*sp)(int) = twice;
    AssertEq(gp(3), 6);
    AssertEq(sp(3), 6);
}

[[cccc::test]]
void test_return_type_binds(void) {
    int (*zi)(void)    = zero;
    double (*zd)(void) = zero;
    AssertEq(zi(), 0);
    AssertEq(zd(), 0.0);
}

[[cccc::test]]
void test_parameters_not_in_return(void) {
    long (*f)(int, long) = second;
    AssertEq(f(1, 41L), 41);
}

[[cccc::test]]
void test_pack(void) {
    int (*c)(int, long) = count;
    int (*z)(void)      = count;
    AssertEq(c(1, 2L), 2);
    AssertEq(z(), 0);
}

// With an overload set, a function of the target's type wins and the template
// answers a target no function has.
static int OVL pick_one(int x) {
    return 1;
}
static int OVL pick_one(char *s) {
    return 2;
}
@template(T) static int pick_one(T x) {
    return 3;
}

[[cccc::test]]
void test_set_function_wins(void) {
    int (*fi)(int)    = pick_one;
    int (*fs)(char *) = pick_one;
    AssertEq(fi(0), 1);
    AssertEq(fs(0), 2);
}

[[cccc::test]]
void test_template_when_no_function_matches(void) {
    int (*fd)(double) = pick_one;
    int (*fl)(long)   = pick_one;
    AssertEq(fd(0.0), 3);
    AssertEq(fl(0), 3);
}

// A set of one function and a template still needs a target type.
static int OVL lone(int x) {
    return 1;
}
@template(T) static int lone(T x) {
    return 2;
}

[[cccc::test]]
void test_one_function_and_template(void) {
    int (*a)(int)    = lone;
    int (*b)(double) = lone;
    AssertEq(a(0), 1);
    AssertEq(b(0), 2);
    AssertEq(lone(0), 1);
}
