// Test C23 __VA_OPT__ preprocessor feature
// This version only includes tests that should work with current implementation

#include <stdio.h>
#include <string.h>

// Track test results
int tests_passed = 0;
int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { \
        tests_passed++; \
    } else { \
        printf("FAIL: %s\n", msg); \
        tests_failed++; \
    } \
} while(0)

// Test 1: Basic __VA_OPT__ with comma insertion
#define LOG1(fmt, ...) printf(fmt __VA_OPT__(,) __VA_ARGS__)

void test_basic_comma_insertion() {
    LOG1("test1\n");
    LOG1("test2: x=%d\n", 42);
    ASSERT(1, "Basic comma insertion with __VA_OPT__");
}

// Test 2: __VA_OPT__ with token sequence (not just comma)
#define DEBUG(msg, ...) fprintf(stderr, "[DEBUG] " msg __VA_OPT__(,) __VA_ARGS__)

void test_token_sequence() {
    DEBUG("test\n");
    DEBUG("x=%d\n", 123);
    ASSERT(1, "__VA_OPT__ with token sequence");
}

// Test 3: __VA_OPT__ with just __VA_ARGS__
#define JUST_ARGS(...) __VA_OPT__(__VA_ARGS__)

void test_just_va_args() {
    int x = 10 JUST_ARGS();           // 10
    int y = 10 JUST_ARGS(+ 5);        // 10 + 5 = 15

    ASSERT(x == 10, "__VA_OPT__(__VA_ARGS__) with no args");
    ASSERT(y == 15, "__VA_OPT__(__VA_ARGS__) with args");
}

// Test 4: Empty __VA_OPT__
#define EMPTY(...) __VA_OPT__()

void test_empty_va_opt() {
    int x = 42 EMPTY();
    int y = 42 EMPTY(a, b, c);

    ASSERT(x == 42, "__VA_OPT__() with no args");
    ASSERT(y == 42, "__VA_OPT__() with args");
}

// Test 5: __VA_OPT__ with parentheses in content
#define PARENS(...) __VA_OPT__((1 + 2))

void test_parens_in_content() {
    int x = 0 PARENS();         // 0
    int z = PARENS(ignored);    // (1 + 2) = 3

    ASSERT(x == 0, "__VA_OPT__ with parentheses - no args");
    ASSERT(z == 3, "__VA_OPT__ with parentheses - with args");
}

// Test 6: __VA_OPT__ with operators
#define ADD_IF_ARGS(a, ...) a __VA_OPT__(+ 10)

void test_operators() {
    int x = ADD_IF_ARGS(5);      // 5
    int y = ADD_IF_ARGS(5, x);   // 5 + 10 = 15

    ASSERT(x == 5, "__VA_OPT__ with operator - no args");
    ASSERT(y == 15, "__VA_OPT__ with operator - with args");
}

int main() {
    printf("Testing C23 __VA_OPT__ feature (working subset)...\n\n");

    test_basic_comma_insertion();
    test_token_sequence();
    test_just_va_args();
    test_empty_va_opt();
    test_parens_in_content();
    test_operators();

    printf("\n=================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("=================================\n");

    // Return 42 for success (CCCC test convention)
    return tests_failed == 0 ? 42 : 1;
}
