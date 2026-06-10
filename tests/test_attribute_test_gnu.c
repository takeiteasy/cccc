// Tests for bare __attribute__((test)) / __attribute__((test_setup)) /
// __attribute__((test_teardown)) forms — #348.
// CCCC_FLAGS: --testing

// Bare __attribute__((test)) without args.
__attribute__((test))
void test_gnu_bare(void) {
    $assert_eq(1 + 1, 2);
}

// __attribute__((test(...))) with suite and name args.
__attribute__((test(suite = "gnu", name = "with args")))
void test_gnu_with_args(void) {
    $assert_eq(6 * 7, 42);
}

// __attribute__((test_setup)) and __attribute__((test_teardown)) bare forms.
static int gnu_setup_count    = 0;
static int gnu_teardown_count = 0;

__attribute__((test_setup))
void gnu_setup(void) { gnu_setup_count++; }

__attribute__((test_teardown))
void gnu_teardown(void) { gnu_teardown_count++; }

__attribute__((test))
void test_gnu_hooks_ran(void) {
    $assert(gnu_setup_count > 0);
}

// __attribute__((test_setup(suite="once_gnu", once))) form.
static int once_count = 0;

__attribute__((test_setup(suite = "once_gnu", once)))
void gnu_once_setup(void) { once_count++; }

__attribute__((test(suite = "once_gnu")))
void test_gnu_once_setup_a(void) {
    $assert_eq(once_count, 1);
}

__attribute__((test(suite = "once_gnu")))
void test_gnu_once_setup_b(void) {
    $assert_eq(once_count, 1);
}

// __has_attribute queries for test/test_setup/test_teardown (#348).
#if !__has_attribute(test)
#error "__has_attribute(test) should be 1"
#endif
#if !__has_attribute(test_setup)
#error "__has_attribute(test_setup) should be 1"
#endif
#if !__has_attribute(test_teardown)
#error "__has_attribute(test_teardown) should be 1"
#endif

// __has_c_attribute with cccc:: vendor prefix.
#if !__has_c_attribute(cccc::test)
#error "__has_c_attribute(cccc::test) should be 1"
#endif
#if !__has_c_attribute(cccc::test_setup)
#error "__has_c_attribute(cccc::test_setup) should be 1"
#endif
#if !__has_c_attribute(cccc::test_teardown)
#error "__has_c_attribute(cccc::test_teardown) should be 1"
#endif
