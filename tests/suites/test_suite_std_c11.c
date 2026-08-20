// CCCC_FLAGS: --testing --std=c11
// Consolidated suite: C11 features valid in --std=c11 mode
// Source tests: test_std_c11_anon_struct_ok, test_std_c11_generic_ok,
//               test_std_c11_static_assert_ok, test_warning_ignored_tls,
//               test_warning_ignored_tls_thread
//
// Deferred (uses -E flag, not [[cccc::test]]-compatible):
// test_attr_target_auto_c11

// File-scope thread-local variables (required by standard — TLS must be
// file/static scope)
_Thread_local int c11_tls_var    = 0;
__thread int      c11_thread_var = 0;

// File-scope struct for anonymous struct test (CCCC promotes local structs to
// file scope)
struct C11OuterAnon {
    int x;
    struct {
        int a;
        int b;
    };
};

#pragma cccc suite begin "std_c11"

// test_std_c11_anon_struct_ok: anonymous structs accepted in C11
[[cccc::test(return = 42)]]
int test_std_c11_anon_struct(void) {
    struct C11OuterAnon o;
    o.x = 1;
    o.a = 2;
    o.b = 3;
    return o.a == 2 ? 42 : 1;
}

// test_std_c11_generic_ok: _Generic is available in C11
[[cccc::test(return = 42)]]
int test_std_c11_generic(void) {
#define myabs(x)                                                               \
    _Generic((x), int: (x) < 0 ? -(x) : (x), double: (x) < 0.0 ? -(x) : (x))
    return myabs(-5) == 5 ? 42 : 1;
#undef myabs
}

// test_std_c11_static_assert_ok: _Static_assert available in C11
[[cccc::test(return = 42)]]
int test_std_c11_static_assert(void) {
    _Static_assert(sizeof(int) >= 4, "int must be at least 4 bytes");
    return 42;
}

// test_warning_ignored_tls: _Thread_local accepted without warning in C11
[[cccc::test(return = 42)]]
int test_warning_ignored_tls(void) {
    c11_tls_var = 42;
    return c11_tls_var;
}

// test_warning_ignored_tls_thread: __thread accepted without warning in C11
[[cccc::test(return = 42)]]
int test_warning_ignored_tls_thread(void) {
    c11_thread_var = 42;
    return c11_thread_var;
}

#pragma cccc suite end
