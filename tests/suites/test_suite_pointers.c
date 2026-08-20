// CCCC_FLAGS: --testing
// Consolidated suite: pointers, function pointers, const, restrict
// Source tests: test_const_comprehensive, test_const_correct_search,
// test_const_pointer_to_const, test_const_ptr, test_funcptr_callbacks,
// test_funcptr_comprehensive, test_funcptr_simple, test_pointers,
// test_restrict_derived_locals, test_restrict_indexed_const,
// test_restrict_memcpy_loop, test_restrict_memcpy_types,
// test_restrict_noalias_opt

#include <string.h>

// [from test_const_comprehensive]
// Comprehensive const test

int global_const = 100;

// [from test_const_correct_search]
// Test C23 const-correct search functions in <string.h> (ticket #396)
// strchr, strrchr, strstr, strpbrk, memchr preserve const-ness of their
// pointer argument in the return type via _Generic dispatch macros.

// [from test_const_pointer_to_const]
// Test pointer to const (can change pointer, not pointee)

// [from test_const_ptr]
// Test const pointer (cannot change pointer, can change pointee)

// [from test_funcptr_callbacks]
// Test: Function pointers with callbacks
// Expected return: 42

// Simple operations

static int add(int a, int b) {
    return a + b;
}

static int sub(int a, int b) {
    return a - b;
}

static int mul(int a, int b) {
    return a * b;
}

// Higher-order function: takes array, applies operation, returns result

static int reduce(int *arr, int len, int init, int (*op)(int, int)) {
    int result = init;
    int i      = 0;
    while (i < len) {
        result = op(result, arr[i]);
        i      = i + 1;
    }
    return result;
}

// Function that conditionally selects operation

static int compute(int x, int y, int use_add) {
    int (*operation)(int, int);

    if (use_add) {
        operation = add;
    } else {
        operation = mul;
    }

    return operation(x, y);
}

// [from test_funcptr_comprehensive]
// Test: Comprehensive function pointer tests
// Expected return: 42

static int _funcptr_comprehensive_add(int a, int b) {
    return a + b;
}

static int subtract(int a, int b) {
    return a - b;
}

static int multiply(int a, int b) {
    return a * b;
}

static int divide(int a, int b) {
    return a / b;
}

// Function that takes a function pointer as parameter

static int apply_op(int (*op)(int, int), int x, int y) {
    return op(x, y);
}

// Function that returns a function pointer
int (*get_operation(int choice))(int, int) {
    if (choice == 1)
        return _funcptr_comprehensive_add;
    else if (choice == 2)
        return subtract;
    else if (choice == 3)
        return multiply;
    else
        return divide;
}

// [from test_funcptr_simple]
// Test: Function pointers - basic usage
// Expected return: 42

static int _funcptr_simple_add(int a, int b) {
    return a + b;
}

static int _funcptr_simple_subtract(int a, int b) {
    return a - b;
}

static int _funcptr_simple_multiply(int a, int b) {
    return a * b;
}

// [from test_restrict_derived_locals]
// Test #269: restrict derived-local alias analysis.
// Locals provably derived from restrict params (q = p + 1) inherit the
// non-aliasing property so the deref cache can treat *q like p[1].

static int tc_restrict_derived_locals_result = 0;

// Basic derived read: *q should be cached as (p, sizeof(int)).
// After storing to *p, the cache for (p, 0) is updated but q's slot is
// unaffected.

static int derived_basic_read(int *restrict p) {
    int *q = p + 1;
    int  a = *q; // loads p[1], caches at (p, sizeof(int))
    *p     = 10; // writes p[0], only invalidates (p,0) slot
    int b  = *q; // should still see p[1] from cache
    return a + b;
}

static void test_derived_basic_read(void) {
    int arr[2] = {0, 7};
    int r      = derived_basic_read(arr);
    if (r != 14) // a==7, b==7 (p[1] unchanged)
        __builtin_trap();
    if (arr[0] != 10)
        __builtin_trap();
    tc_restrict_derived_locals_result++;
}

// Write-through via derived local: store through *q must update cache at (p,
// 4).

static int derived_write_through(int *restrict p) {
    int *q = p + 1;
    int  a = p[1]; // load p[1], cache (p, sizeof(int)) = 7
    *q     = 99;   // store through derived local: must write-through cache slot
    return p[1];   // must see 99, not stale 7
}

static void test_derived_write_through(void) {
    int arr[2] = {0, 7};
    int r      = derived_write_through(arr);
    if (r != 99)
        __builtin_trap();
    tc_restrict_derived_locals_result++;
}

// No over-invalidation: store through derived q must not touch s's cache slot.

static int derived_no_overinvalidate(int *restrict p, int *restrict s) {
    int *q = p + 1;
    int  a = *s;  // cache *s
    *q     = 99;  // store via derived q → invalidates only p's slots, not s's
    int b  = *s;  // must still see cached *s value
    return a + b; // == 2 * original *s
}

static void test_derived_no_overinvalidate(void) {
    int x = 5, y = 0;
    int arr[2] = {0, 0};
    int r      = derived_no_overinvalidate(arr, &x);
    if (r != 10) // a==5, b==5
        __builtin_trap();
    tc_restrict_derived_locals_result++;
}

// Negative offset: q = p - 1 (pointing one element before p).

static int derived_negative_offset(int *restrict p) {
    int *q = p - 1;
    int  a = *q; // reads p[-1]
    return a;
}

static void test_derived_negative_offset(void) {
    int arr[3] = {3, 7, 0};
    // Pass &arr[1] so q = &arr[0]
    int r = derived_negative_offset(&arr[1]);
    if (r != 3)
        __builtin_trap();
    tc_restrict_derived_locals_result++;
}

// Bail case — variable offset: q = p + n. q must not be cached but store
// through *q must still invalidate p's slots (not do a global invalidate).

static int derived_variable_offset(int *restrict p, int *restrict s, int n) {
    int *q = p + n;
    int  a = *s; // cache *s
    *q = 99; // store via unknown-offset derived ptr → invalidate p's slots only
    int b = *s; // s's cache must be intact
    return a + b;
}

static void test_derived_variable_offset(void) {
    int x      = 5;
    int arr[4] = {0, 0, 0, 0};
    int r      = derived_variable_offset(arr, &x, 2);
    if (r != 10) // a==5, b==5, *s unchanged
        __builtin_trap();
    if (arr[2] != 99)
        __builtin_trap();
    tc_restrict_derived_locals_result++;
}

// Bail case — address of q taken: q not tracked, reads go through normal path.

static int derived_addr_taken(int *restrict p) {
    int  *q = p + 1;
    int **r = &q;   // q's address is taken; q must not be in the derivation map
    int   a = *q;
    *p      = 0;    // modifies p[0], not p[1]
    return a + **r; // a and **r both == p[1], tc_restrict_derived_locals_result
                    // == 2 * p[1]
}

static void test_derived_addr_taken(void) {
    int arr[2] = {0, 21};
    int r      = derived_addr_taken(arr);
    if (r != 42)
        __builtin_trap();
    tc_restrict_derived_locals_result++;
}

// Bail case — multiple assignments to q: q not tracked.

static int derived_multi_assign(int *restrict p) {
    int *q = p + 1;
    q      = p + 2; // second assignment → q is not tracked
    return *q;      // reads p[2]; normal load path
}

static void test_derived_multi_assign(void) {
    int arr[3] = {0, 7, 42};
    int r      = derived_multi_assign(arr);
    if (r != 42)
        __builtin_trap();
    tc_restrict_derived_locals_result++;
}

// [from test_restrict_indexed_const]
// Test p[const] restrict deref cache extension (#267 follow-on).
// Cache key is (restrict_param, byte_offset); p[0], p[1], etc. get separate
// slots.

static int tc_restrict_indexed_const_result = 0;

// ------------------------------------------------------------------
// Basic: p[const] is served from cache across non-aliasing *q store
// ------------------------------------------------------------------

static int indexed_no_invalidate(int *restrict p, int *restrict q) {
    int a = p[1]; // miss: load p[1], cache (p,4)
    *q    = 99;   // non-aliasing: must NOT invalidate p's cache
    int b = p[1]; // hit: must return same value as a
    return a + b; // a == b if cache served correctly
}

static void test_indexed_const_no_invalidate_by_q(void) {
    int data[4] = {10, 20, 30, 40};
    int other   = 0;
    int r       = indexed_no_invalidate(data, &other);
    // a==20, b==20 → r==40; if cache miss b would still be 20, but
    // we can't differentiate here — the invalidation test is the value check.
    if (r != 40)
        __builtin_trap();
    tc_restrict_indexed_const_result++;
}

// ------------------------------------------------------------------
// Two distinct offsets cached simultaneously
// ------------------------------------------------------------------

static int two_offsets(int *restrict p, int *restrict q) {
    int a = p[0]; // cache (p,0)
    int b = p[2]; // cache (p,8)
    *q    = 55;   // non-aliasing
    int c = p[0]; // hit (p,0)
    int d = p[2]; // hit (p,8)
    return (a == c && b == d) ? 1 : 0;
}

static void test_two_offsets_coexist(void) {
    int data[4] = {1, 2, 3, 4};
    int other   = 0;
    if (!two_offsets(data, &other))
        __builtin_trap();
    tc_restrict_indexed_const_result++;
}

// ------------------------------------------------------------------
// Constant-indexed store write-through to the correct cache entry
// ------------------------------------------------------------------

static int const_store_write_through(int *restrict p) {
    int a = p[0]; // cache (p,0) = 10
    int b = p[2]; // cache (p,8) = 30
    p[0]  = 99;   // write-through (p,0)
    int c = p[0]; // should be 99
    int d = p[2]; // (p,8) untouched → still 30
    return (a == 10 && b == 30 && c == 99 && d == 30) ? 1 : 0;
}

static void test_const_store_updates_entry(void) {
    int data[4] = {10, 20, 30, 40};
    if (!const_store_write_through(data))
        __builtin_trap();
    tc_restrict_indexed_const_result++;
}

// ------------------------------------------------------------------
// Constant store to a DIFFERENT offset does NOT invalidate other offset
// ------------------------------------------------------------------

static int const_store_other_offset(int *restrict p) {
    int a = p[0]; // cache (p,0) = 5
    p[1]  = 100;  // write-through (p,4); (p,0) entry must be unaffected
    int b = p[0]; // hit: should still be 5
    return (a == 5 && b == 5) ? 1 : 0;
}

static void test_const_store_other_offset_no_invalidate(void) {
    int data[4] = {5, 6, 7, 8};
    if (!const_store_other_offset(data))
        __builtin_trap();
    tc_restrict_indexed_const_result++;
}

// ------------------------------------------------------------------
// Variable-index store invalidates ALL cached offsets for that param
// ------------------------------------------------------------------

static int variable_index_invalidates(int *restrict p, int i) {
    int a = p[0]; // cache (p,0)
    int b = p[1]; // cache (p,4)
    p[i]  = 99;   // variable index: invalidates all p entries
    int c = p[0]; // must reload (i==0 → data[0]==99)
    int d = p[1]; // must reload (data[1] unchanged)
    return (a == 10 && b == 20 && c == 99 && d == 20) ? 1 : 0;
}

static void test_variable_index_store_invalidates_all(void) {
    int data[4] = {10, 20, 30, 40};
    if (!variable_index_invalidates(data, 0))
        __builtin_trap();
    tc_restrict_indexed_const_result++;
}

// ------------------------------------------------------------------
// char *restrict: width normalization at constant offset
// ------------------------------------------------------------------

static int char_restrict_indexed(signed char *restrict p, int *restrict q) {
    int a = p[1]; // cache (p,1) = 2
    *q    = 55;
    int b = p[1]; // hit
    return (a == 2 && b == 2) ? 1 : 0;
}

static void test_char_restrict_indexed_const(void) {
    signed char data[4] = {-1, 2, -3, 4};
    int         other   = 0;
    if (!char_restrict_indexed(data, &other))
        __builtin_trap();
    tc_restrict_indexed_const_result++;
}

// ------------------------------------------------------------------
// Derived pointer store conservatively invalidates all entries
// ------------------------------------------------------------------

static int derived_ptr_invalidates(int *restrict p) {
    int  a = p[0]; // cache (p,0)
    int *r = p;    // derived, non-restrict
    *r     = 99;   // unknown base → invalidate all
    int b  = p[0]; // must reload: 99
    return (a == 10 && b == 99) ? 1 : 0;
}

static void test_derived_pointer_store_invalidates(void) {
    int data[4] = {10, 20, 30, 40};
    if (!derived_ptr_invalidates(data))
        __builtin_trap();
    tc_restrict_indexed_const_result++;
}

// ------------------------------------------------------------------
// Plain *p (offset 0) still works identically after the refactor
// ------------------------------------------------------------------

static int plain_deref_still_works(int *restrict p, int *restrict q) {
    int a = *p; // cache (p,0)
    *q    = 99; // non-aliasing
    int b = *p; // hit
    return (a == 42 && b == 42) ? 1 : 0;
}

static void test_plain_deref_still_works(void) {
    int x = 42, other = 0;
    if (!plain_deref_still_works(&x, &other))
        __builtin_trap();
    tc_restrict_indexed_const_result++;
}

// ------------------------------------------------------------------
// Array parameter with restrict qualifier (post-#397 decay enables cache)
// ------------------------------------------------------------------

static int sum_first_two(int n, int a[restrict n]) {
    int x = a[0];
    int y = a[1];
    return x + y;
}

static void test_array_param_restrict_decay(void) {
    int data[4] = {3, 7, 0, 0};
    if (sum_first_two(4, data) != 10)
        __builtin_trap();
    tc_restrict_indexed_const_result++;
}

// [from test_restrict_memcpy_loop]
// Test #268: restrict memcpy loop lowering.
// for (size_t i = 0; i < n; i++) dst[i] = src[i]
// with restrict dst and src must be lowered to MCPY.

static int tc_restrict_memcpy_loop_passed = 0;

// Basic byte copy

static void byte_copy(char *restrict dst, const char *restrict src, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = src[i];
}

static void test_byte_copy(void) {
    char src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    char dst[8] = {0};
    byte_copy(dst, src, 8);
    for (int i = 0; i < 8; i++)
        if (dst[i] != src[i])
            __builtin_trap();
    tc_restrict_memcpy_loop_passed++;
}

// Int copy

static void int_copy(int *restrict dst, const int *restrict src, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = src[i];
}

static void test_int_copy(void) {
    int src[5] = {10, 20, 30, 40, 50};
    int dst[5] = {0};
    int_copy(dst, src, 5);
    for (int i = 0; i < 5; i++)
        if (dst[i] != src[i])
            __builtin_trap();
    tc_restrict_memcpy_loop_passed++;
}

// Long copy

static void long_copy(long *restrict dst, const long *restrict src, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = src[i];
}

static void test_long_copy(void) {
    long src[4] = {100, 200, 300, 400};
    long dst[4] = {0};
    long_copy(dst, src, 4);
    for (int i = 0; i < 4; i++)
        if (dst[i] != src[i])
            __builtin_trap();
    tc_restrict_memcpy_loop_passed++;
}

// Non-restrict loop must still work correctly (uses the regular loop path)

static void non_restrict_copy(int *dst, const int *src, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = src[i];
}

static void test_non_restrict_copy(void) {
    int src[3] = {7, 8, 9};
    int dst[3] = {0};
    non_restrict_copy(dst, src, 3);
    for (int i = 0; i < 3; i++)
        if (dst[i] != src[i])
            __builtin_trap();
    tc_restrict_memcpy_loop_passed++;
}

// Copy of zero elements must be a no-op

static void test_zero_len(void) {
    int src[2] = {1, 2};
    int dst[2] = {99, 99};
    int_copy(dst, src, 0);
    if (dst[0] != 99 || dst[1] != 99)
        __builtin_trap();
    tc_restrict_memcpy_loop_passed++;
}

// size_t induction variable

static void sizet_copy(long *restrict dst, const long *restrict src, long n) {
    for (long i = 0; i < n; i++)
        dst[i] = src[i];
}

static void test_sizet_copy(void) {
    long src[3] = {11, 22, 33};
    long dst[3] = {0};
    sizet_copy(dst, src, 3);
    for (int i = 0; i < 3; i++)
        if (dst[i] != src[i])
            __builtin_trap();
    tc_restrict_memcpy_loop_passed++;
}

// [from test_restrict_memcpy_types]
// Test #268: restrict memcpy lowering for various element types.
// Verifies correctness after lowering across common element widths.

static int tc_restrict_memcpy_types_passed = 0;

static void short_copy(short *restrict dst, const short *restrict src, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = src[i];
}

static void test_short(void) {
    short src[4] = {-1, 0, 32767, -32768};
    short dst[4] = {0};
    short_copy(dst, src, 4);
    for (int i = 0; i < 4; i++)
        if (dst[i] != src[i])
            __builtin_trap();
    tc_restrict_memcpy_types_passed++;
}

static void uint_copy(unsigned *restrict dst, const unsigned *restrict src,
                      int n) {
    for (int i = 0; i < n; i++)
        dst[i] = src[i];
}

static void test_uint(void) {
    unsigned src[3] = {0, 0xFFFFFFFFu, 42};
    unsigned dst[3] = {0};
    uint_copy(dst, src, 3);
    for (int i = 0; i < 3; i++)
        if (dst[i] != src[i])
            __builtin_trap();
    tc_restrict_memcpy_types_passed++;
}

static void ulong_copy(unsigned long *restrict dst,
                       const unsigned long *restrict src, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = src[i];
}

static void test_ulong(void) {
    unsigned long src[2] = {0xDEADBEEFCAFEBABEUL, 0UL};
    unsigned long dst[2] = {0};
    ulong_copy(dst, src, 2);
    for (int i = 0; i < 2; i++)
        if (dst[i] != src[i])
            __builtin_trap();
    tc_restrict_memcpy_types_passed++;
}

// Pointer copy

static void ptr_copy(void **restrict dst, void *const *restrict src, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = src[i];
}

static void test_ptr(void) {
    int   a, b, c;
    void *src[3] = {&a, &b, &c};
    void *dst[3] = {0};
    ptr_copy(dst, src, 3);
    for (int i = 0; i < 3; i++)
        if (dst[i] != src[i])
            __builtin_trap();
    tc_restrict_memcpy_types_passed++;
}

// [from test_restrict_noalias_opt]
// Test #267: restrict deref cache — store through *q must not invalidate *p
// cache. Both p and q are restrict-qualified scalar pointer params (different
// objects).

static int tc_restrict_noalias_opt_result = 0;

// Read *p, write *q, read *p again: the second read should use the cached
// value.

static int noalias_test(int *restrict p, int *restrict q) {
    int a = *p;   // load *p, cache it
    *q    = 99;   // write to *q — must NOT invalidate p's cache
    int b = *p;   // should use cached value (same as a)
    return a + b; // == 2 * (*p original)
}

// Verify that the restrict contract is upheld: p and q point to different
// objects.

static void test_basic_noalias(void) {
    int x = 21, y = 0;
    int r = noalias_test(&x, &y);
    // a == 21, b == 21 (p wasn't modified through q), so r == 42
    if (r != 42)
        __builtin_trap();
    if (y != 99)
        __builtin_trap();
    tc_restrict_noalias_opt_result++;
}

// Multiple restrict params: p, q, r — stores through q and r must not
// invalidate p's cached value.

static int three_restrict(int *restrict p, int *restrict q, int *restrict r) {
    int a = *p;
    *q    = 1;
    *r    = 2;
    int b = *p;   // should still be the original value of *p
    return b - a; // should be 0
}

static void test_three_params(void) {
    int x = 10, y = 0, z = 0;
    int r = three_restrict(&x, &y, &z);
    if (r != 0)
        __builtin_trap();
    if (y != 1 || z != 2)
        __builtin_trap();
    tc_restrict_noalias_opt_result++;
}

// A write through *p itself must update the cache (not leave stale value).

static int write_through_p(int *restrict p, int *restrict q) {
    int a = *p;    // cache *p = a
    *p    = a + 1; // write new value to *p
    *q    = 99;    // unrelated write
    int b = *p;    // must see a+1, not the stale cached a
    return b;
}

static void test_write_through_p(void) {
    int x = 5, y = 0;
    int r = write_through_p(&x, &y);
    if (r != 6)
        __builtin_trap();
    if (x != 6)
        __builtin_trap();
    tc_restrict_noalias_opt_result++;
}

// Restrict on char pointers.

static int char_restrict(char *restrict p, char *restrict q) {
    char a = *p;
    *q     = 'X';
    char b = *p;
    return (b == a) ? 1 : 0;
}

static void test_char_restrict(void) {
    char x = 'A', y = 0;
    if (!char_restrict(&x, &y))
        __builtin_trap();
    if (y != 'X')
        __builtin_trap();
    tc_restrict_noalias_opt_result++;
}

// A pointer derived from a restrict param is still "based on" that param
// (C11 6.7.3.1). Storing through the derived pointer must update / invalidate
// the restrict cache.

static int derived_ptr_store(int *restrict p, int *restrict q) {
    (void)q;
    int  a = *p;     // cache *p = a
    int *r = p;      // r is based on p
    *r     = a + 10; // modifies *p through the derived pointer; must not leave
                     // stale cache
    return *p;       // must return a+10, not the stale cached a
}

static void test_derived_ptr_store(void) {
    int x = 5, y = 0;
    int r = derived_ptr_store(&x, &y);
    // a=5, *p set to 15 through r, so *p must return 15
    if (r != 15)
        __builtin_trap();
    if (x != 15)
        __builtin_trap();
    tc_restrict_noalias_opt_result++;
}

// Storing a wide value through a char *restrict must truncate the cache entry.
// STR_B only writes the low byte; the cache must reflect the truncated value.

static int char_store_truncation(char *restrict p, int wide) {
    *p =
        (char)wide; // wide may have bits above 0xFF; STR_B stores low byte only
    int b =
        *p; // must return sign/zero-extended low byte, not the raw wide value
    return b;
}

static void test_char_store_truncation(void) {
    char x = 0;
    // 0x141 truncated to char (signed) is 0x41 = 65
    int r = char_store_truncation(&x, 0x141);
    if (r != (char)0x141) // == 65
        __builtin_trap();
    if (x != (char)0x141)
        __builtin_trap();
    tc_restrict_noalias_opt_result++;
}

#pragma cccc suite begin "pointers"

// test_const_comprehensive
[[cccc::test(return = 42)]]
int test_const_comprehensive(void) {
    // Test 1: Basic const variable
    const int x = 10;
    int       a = x + 5; // Reading const is OK

    // Test 2: Pointer to const
    int        y    = 20;
    const int *p1   = &y;
    int        val1 = *p1; // Reading is OK

    // Test 3: Const pointer (can modify pointee)
    int        z  = 30;
    int *const p2 = &z;
    *p2           = 35;

    // Test 4: Const pointer to const
    const int *const p3   = &x;
    int              val2 = *p3; // Reading is OK

    // Test 5: Global const
    int val3 = global_const;

    // Result: 10 + 5 + 20 + 35 + 10 + 100 = 180
    int result = a + val1 + *p2 + val2 + val3;
    if (result != 180)
        return 1; // Assert result == 180
    return 42;
}

// test_const_correct_search
[[cccc::test(return = 42)]]
int test_const_correct_search(void) {
    /* const path: result must be assignable to const pointer */
    const char *s  = "hello world";
    const char *p1 = strchr(s, 'o');
    const char *p2 = strrchr(s, 'o');
    const char *p3 = strstr(s, "world");
    const char *p4 = strpbrk(s, "aeiou");
    /* memchr always returns void * (implicitly assignable to const void *) */
    void *p5 = memchr(s, 'o', 11);
    if (!p1 || !p2 || !p3 || !p4 || !p5)
        return 1;

    /* check correctness of returned positions */
    if (*p1 != 'o')
        return 2; /* first 'o' in "hello world" */
    if (*(p1 + 1) != ' ')
        return 3; /* 'o' at index 7, next is space */
    if (*p2 != 'o')
        return 4; /* last 'o' in "hello world" */
    if (p2 <= p1)
        return 5; /* last 'o' is after first 'o' */
    if (*p3 != 'w')
        return 6; /* strstr points to "world" */
    if (*p4 != 'e')
        return 7; /* first vowel in "hello world" is 'e' */

    /* non-const path: result must be assignable to non-const pointer */
    char  buf[] = "hello world";
    char *q1    = strchr(buf, 'o');
    char *q2    = strrchr(buf, 'o');
    char *q3    = strstr(buf, "world");
    char *q4    = strpbrk(buf, "aeiou");
    void *q5    = memchr(buf, 'o', 11);
    if (!q1 || !q2 || !q3 || !q4 || !q5)
        return 8;
    (void)q5;

    /* verify non-const result is writable */
    *q1 = 'O';
    if (buf[4] != 'O')
        return 9; /* replaced 'o' in "hellO world" */

    /* strchr returning NULL when not found */
    if (strchr(s, 'z') != 0)
        return 10;
    if (strstr(s, "xyz") != 0)
        return 11;
    if (strpbrk(s, "xyz") != 0)
        return 12;

    return 42;
}

// test_const_pointer_to_const
[[cccc::test(return = 42)]]
int test_const_pointer_to_const(void) {
    int        x = 10;
    int        y = 20;

    const int *p = &x; // Pointer to const int
    p            = &y; // OK: can change pointer
    // *p = 30;         // ERROR: cannot modify through pointer to const

    if (*p != 20)
        return 1; // Assert *p == 20
    return 42;
}

// test_const_ptr
[[cccc::test(return = 42)]]
int test_const_ptr(void) {
    int        x = 10;
    int        y = 20;

    int *const p = &x; // Const pointer to int
    *p           = 30; // OK: can modify through pointer
    // p = &y;          // ERROR: cannot change const pointer

    if (*p != 30)
        return 1; // Assert *p == 30
    return 42;
}

// test_funcptr_callbacks
[[cccc::test(return = 42)]]
int test_funcptr_callbacks(void) {
    // Test 1: Callback-style with reduce
    int numbers[5];
    numbers[0]  = 1;
    numbers[1]  = 2;
    numbers[2]  = 3;
    numbers[3]  = 4;
    numbers[4]  = 5;

    int sum     = reduce(numbers, 5, 0, add); // 0+1+2+3+4+5 = 15
    int product = reduce(numbers, 5, 1, mul); // 1*1*2*3*4*5 = 120

    // Test 2: Conditional function selection
    int r1 = compute(20, 22, 1); // add: 42
    int r2 = compute(21, 2, 0);  // mul: 42

    // Test 3: Function pointer in struct (using array as workaround)
    int (*ops[2])(int, int);
    ops[0] = add;
    ops[1] = sub;

    int r3 = ops[0](12, 30); // 42
    int r4 = ops[1](50, 8);  // 42

    // Return 42 if any test succeeded
    if (r1 == 42)
        return 42;
    if (r2 == 42)
        return 42;
    if (r3 == 42)
        return 42;
    if (r4 == 42)
        return 42;

    return sum + product; // Shouldn't reach
}

// test_funcptr_comprehensive
[[cccc::test(return = 42)]]
int test_funcptr_comprehensive(void) {
    // Test 1: Basic function pointer
    int (*func_ptr)(int, int);
    func_ptr = &_funcptr_comprehensive_add;
    int r1   = func_ptr(10, 20); // Should be 30

    // Test 2: Reassign function pointer
    func_ptr = subtract;
    int r2   = func_ptr(50, 8); // Should be 42

    // Test 3: Function pointer without explicit &
    func_ptr = multiply;
    int r3   = func_ptr(6, 7); // Should be 42

    // Test 4: Array of function pointers
    int (*ops[4])(int, int);
    ops[0] = _funcptr_comprehensive_add;
    ops[1] = subtract;
    ops[2] = multiply;
    ops[3] = divide;

    int r4 = ops[1](50, 8); // Should be 42
    int r5 = ops[3](84, 2); // Should be 42

    // Test 5: Function pointer as parameter
    int r6 = apply_op(_funcptr_comprehensive_add, 12, 30); // Should be 42
    int r7 = apply_op(multiply, 21, 2);                    // Should be 42

    // Test 6: Function returning function pointer
    int (*op_func)(int, int) = get_operation(1);
    int r8  = op_func(22, 20); // Should be 42 (_funcptr_comprehensive_add)

    op_func = get_operation(2);
    int r9  = op_func(50, 8);  // Should be 42 (subtract)

    // Return 42 if any test passed
    if (r2 == 42)
        return 42;
    if (r3 == 42)
        return 42;
    if (r4 == 42)
        return 42;
    if (r5 == 42)
        return 42;
    if (r6 == 42)
        return 42;
    if (r7 == 42)
        return 42;
    if (r8 == 42)
        return 42;
    if (r9 == 42)
        return 42;

    return r1; // Shouldn't reach here
}

// test_funcptr_simple
[[cccc::test(return = 42)]]
int test_funcptr_simple(void) {
    // Test 1: Basic function pointer declaration and call
    int (*func_ptr)(int, int);

    func_ptr    = &_funcptr_simple_add;
    int result1 = func_ptr(10, 20); // Should be 30

    // Test 2: Assign different function
    func_ptr    = &_funcptr_simple_subtract;
    int result2 = func_ptr(50, 8); // Should be 42

    // Test 3: Direct assignment without &
    func_ptr    = _funcptr_simple_multiply;
    int result3 = func_ptr(6, 7); // Should be 42

    // Return 42 if result2 or result3 is 42
    if (result2 == 42)
        return 42;
    if (result3 == 42)
        return 42;

    return result1; // Shouldn't reach here in passing test
}

// test_pointers
[[cccc::test(return = 42)]]
int test_pointers(void) {
    int  a = 42;
    int *p;

    // Test address-of and dereference
    p     = &a; // Get address of a
    int b = *p; // Dereference pointer

    // Modify through pointer
    *p = 100;

    // a should now be 100
    if (a != 100)
        return 1; // Assert a == 100
    return 42;
}

// test_restrict_derived_locals
[[cccc::test(return = 42)]]
int test_restrict_derived_locals(void) {
    test_derived_basic_read();
    test_derived_write_through();
    test_derived_no_overinvalidate();
    test_derived_negative_offset();
    test_derived_variable_offset();
    test_derived_addr_taken();
    test_derived_multi_assign();
    if (tc_restrict_derived_locals_result != 7)
        return 1;
    return 42;
}

// test_restrict_indexed_const
[[cccc::test(return = 42)]]
int test_restrict_indexed_const(void) {
    test_indexed_const_no_invalidate_by_q();
    test_two_offsets_coexist();
    test_const_store_updates_entry();
    test_const_store_other_offset_no_invalidate();
    test_variable_index_store_invalidates_all();
    test_char_restrict_indexed_const();
    test_derived_pointer_store_invalidates();
    test_plain_deref_still_works();
    test_array_param_restrict_decay();
    if (tc_restrict_indexed_const_result != 9)
        return 1;
    return 42;
}

// test_restrict_memcpy_loop
[[cccc::test(return = 42)]]
int test_restrict_memcpy_loop(void) {
    test_byte_copy();
    test_int_copy();
    test_long_copy();
    test_non_restrict_copy();
    test_zero_len();
    test_sizet_copy();
    if (tc_restrict_memcpy_loop_passed != 6)
        return 1;
    return 42;
}

// test_restrict_memcpy_types
[[cccc::test(return = 42)]]
int test_restrict_memcpy_types(void) {
    test_short();
    test_uint();
    test_ulong();
    test_ptr();
    if (tc_restrict_memcpy_types_passed != 4)
        return 1;
    return 42;
}

// test_restrict_noalias_opt
[[cccc::test(return = 42)]]
int test_restrict_noalias_opt(void) {
    test_basic_noalias();
    test_three_params();
    test_write_through_p();
    test_char_restrict();
    test_derived_ptr_store();
    test_char_store_truncation();
    // All 6 subtests passed
    if (tc_restrict_noalias_opt_result != 6)
        return 1;
    return 42;
}

#pragma cccc suite end
