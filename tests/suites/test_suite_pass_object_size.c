// CCCC_FLAGS: --testing
// Suite: pass_object_size, whose hidden argument carries the caller's object
// size into the callee.

#include <stdlib.h>

#define PS(type) __attribute__((pass_object_size(type)))

struct Pair {
    char name[5];
    int  id;
};

static size_t whole(const void *const p PS(0)) {
    return __builtin_object_size(p, 0);
}

static size_t member(const char *const p PS(1)) {
    return __builtin_object_size(p, 1);
}

[[cccc::test]]
void test_callee_sees_the_callers_size(void) {
    char        buf[23];
    struct Pair pair;
    AssertEq(whole(buf), 23);
    AssertEq(whole(buf + 3), 20);
    AssertEq(whole(&pair), sizeof pair);
    AssertEq(member(pair.name), 5);
}

static size_t through(const char *const p PS(0)) {
    return whole(p);
}

[[cccc::test]]
void test_size_passes_on(void) {
    char buf[9];
    AssertEq(through(buf), 9);
}

[[cccc::test]]
void test_unknown_size(void) {
    char *p = getenv("PATH");
    AssertEq(whole(p), (size_t)-1);
}

static size_t dynamic(const void *const p
                      __attribute__((pass_dynamic_object_size(0)))) {
    return __builtin_dynamic_object_size(p, 0);
}

[[cccc::test]]
void test_dynamic_size(void) {
    char buf[40];
    AssertEq(dynamic(buf), 40);
    AssertEq(dynamic(buf + 10), 30);
}

static size_t qualified(void *const __attribute__((pass_object_size(0))) p) {
    return __builtin_object_size(p, 0);
}

static size_t leading(__attribute__((pass_object_size(0))) void *const p) {
    return __builtin_object_size(p, 0);
}

[[cccc::test]]
void test_every_position(void) {
    char buf[11];
    AssertEq(qualified(buf), 11);
    AssertEq(leading(buf), 11);
}

static size_t declared(const char *const p PS(0));

[[cccc::test]]
void test_declared_before_definition(void) {
    char buf[6];
    AssertEq(declared(buf), 6);
}

static size_t declared(const char *const p PS(0)) {
    return __builtin_object_size(p, 0);
}

static size_t va_sized(const char *const p PS(0), ...) {
    return __builtin_object_size(p, 0);
}

[[cccc::test]]
void test_variadic(void) {
    char buf[13];
    AssertEq(va_sized(buf, 1, 2.0, "three"), 13);
}

__attribute__((overloadable)) static int prefer(const char *const p PS(0)) {
    return 1;
}

__attribute__((overloadable)) static int prefer(const char *const p) {
    return 2;
}

[[cccc::test]]
void test_overload_prefers_pass_object_size(void) {
    char buf[4];
    AssertEq(prefer(buf), 1);
}

[[cccc::test]]
void test_feature_tests(void) {
#if __has_attribute(pass_object_size) &&                                       \
    __has_attribute(pass_dynamic_object_size) &&                               \
    __has_c_attribute(clang::pass_object_size) &&                              \
    __has_c_attribute(clang::pass_dynamic_object_size)
    AssertTrue(1);
#else
    AssertFail();
#endif
}
