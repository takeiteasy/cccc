// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: calls of a name that is both an overload set and a function template
// that no candidate selects.

#define OVL __attribute__((overloadable))

static int OVL f(int x) {
    return 1;
}
static int OVL f(long x) {
    return 2;
}
@template(T) static int f(T *p) {
    return 10;
}

[[cccc::test(error = "no matching function for call to 'f'")]]
void test_no_candidate_fits(void) {
    f("a", "b");
}

[[cccc::test(error = "candidate template: f(int *)")]]
void test_candidate_template_is_listed(void) {
    int a = 0;
    f(&a, 1);
}

static int OVL g(int a, long b) {
    return 1;
}
static int OVL g(long a, int b) {
    return 2;
}
@template(T) static int g(T a, int b) {
    return 10;
}

[[cccc::test(error = "call to 'g' is ambiguous")]]
void test_ambiguous_with_template(void) {
    g(1L, 2L);
}

static int OVL k(int a) {
    return 1;
}
@template(T) static int k(T a) {
    return 10;
}

[[cccc::test(error = "no matching function for call to 'k'")]]
void test_keyed_argument_skips_template(void) {
    k(.b = 2.0);
}

[[cccc::test(error = "no overloaded function 'f' matches the target type")]]
void test_bare_name_is_not_a_template_instance(void) {
    void *p = f;
}
