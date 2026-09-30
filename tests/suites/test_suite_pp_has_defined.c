// CCCC_FLAGS: --testing
//
// The __has_* operators are visible to `defined` / #ifdef / #ifndef and
// evaluate outside #if, matching clang and gcc.

static int count_ifdef(void) {
    int n = 0;
#ifdef __has_include
    n++;
#endif
#ifdef __has_include_next
    n++;
#endif
#ifdef __has_embed
    n++;
#endif
#ifdef __has_feature
    n++;
#endif
#ifdef __has_extension
    n++;
#endif
#ifdef __has_attribute
    n++;
#endif
#ifdef __has_builtin
    n++;
#endif
#ifdef __has_c_attribute
    n++;
#endif
#ifdef __has_cpp_attribute
    n++;
#endif
#ifdef __has_declspec_attribute
    n++;
#endif
#ifdef __has_warning
    n++;
#endif
    return n;
}

static int count_defined(void) {
    int n = 0;
#if defined(__has_include) && defined __has_include_next &&                    \
    defined(__has_embed) && defined __has_feature &&                           \
    defined(__has_extension) && defined __has_attribute &&                     \
    defined(__has_builtin) && defined __has_c_attribute &&                     \
    defined(__has_cpp_attribute) && defined __has_declspec_attribute &&        \
    defined(__has_warning)
    n++;
#endif
#ifndef __has_builtin
    n += 100;
#endif
#ifndef __has_not_an_operator
    n++;
#endif
    return n;
}

// The documented guard: valid where the operator doesn't exist too.
#if defined(__has_c_attribute)
#if __has_c_attribute(cccc::kernel)
#define KERNEL_CHECK [[cccc::kernel]]
#endif
#endif
#ifndef KERNEL_CHECK
#define KERNEL_CHECK
#endif

KERNEL_CHECK static int kernel_add(int a, int b) {
    return a + b;
}

// A macro expansion revealing `defined <operator>`.
#define HAS_DEFINED(x) defined(x)
#if HAS_DEFINED(__has_builtin)
#define REVEALED 1
#else
#define REVEALED 0
#endif

// Fallback blocks written for compilers without the operator are skipped.
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

[[cccc::test]]
void test_pp_has_defined(void) {
    AssertEq(count_ifdef(), 11);
    AssertEq(count_defined(), 2);
    AssertEq(kernel_add(1, 2), 3);
    AssertEq(REVEALED, 1);
}

[[cccc::test]]
void test_pp_has_outside_if(void) {
    AssertEq(__has_builtin(__builtin_expect), 1);
    AssertEq(__has_builtin(__not_a_builtin_xyz), 0);
    AssertEq(__has_attribute(noreturn), 1);
    AssertEq(__has_c_attribute(cccc::kernel) != 0, 1);
    AssertEq(__has_c_attribute(nodiscard) != 0, 1);
    AssertEq(__has_cpp_attribute(nodiscard), 0);
    AssertEq(__has_feature(c_static_assert), 1);
    AssertEq(__has_include(<stdio.h>), 1);
    AssertEq(__has_include(<not_a_real_header_xyz.h>), 0);
    AssertEq(__has_warning("-Wall"), 0);
}

#define HAS_ALIAS __has_builtin

[[cccc::test]]
void test_pp_has_through_macro(void) {
    AssertEq(HAS_ALIAS(__builtin_expect), 1);
}
