// Test p[const] restrict deref cache extension (#267 follow-on).
// Cache key is (restrict_param, byte_offset); p[0], p[1], etc. get separate slots.

static int result = 0;

// ------------------------------------------------------------------
// Basic: p[const] is served from cache across non-aliasing *q store
// ------------------------------------------------------------------
static int indexed_no_invalidate(int *restrict p, int *restrict q) {
    int a = p[1];   // miss: load p[1], cache (p,4)
    *q = 99;        // non-aliasing: must NOT invalidate p's cache
    int b = p[1];   // hit: must return same value as a
    return a + b;   // a == b if cache served correctly
}

static void test_indexed_const_no_invalidate_by_q(void) {
    int data[4] = {10, 20, 30, 40};
    int other = 0;
    int r = indexed_no_invalidate(data, &other);
    // a==20, b==20 → r==40; if cache miss b would still be 20, but
    // we can't differentiate here — the invalidation test is the value check.
    if (r != 40)
        __builtin_trap();
    result++;
}

// ------------------------------------------------------------------
// Two distinct offsets cached simultaneously
// ------------------------------------------------------------------
static int two_offsets(int *restrict p, int *restrict q) {
    int a = p[0];   // cache (p,0)
    int b = p[2];   // cache (p,8)
    *q = 55;        // non-aliasing
    int c = p[0];   // hit (p,0)
    int d = p[2];   // hit (p,8)
    return (a == c && b == d) ? 1 : 0;
}

static void test_two_offsets_coexist(void) {
    int data[4] = {1, 2, 3, 4};
    int other = 0;
    if (!two_offsets(data, &other))
        __builtin_trap();
    result++;
}

// ------------------------------------------------------------------
// Constant-indexed store write-through to the correct cache entry
// ------------------------------------------------------------------
static int const_store_write_through(int *restrict p) {
    int a = p[0];   // cache (p,0) = 10
    int b = p[2];   // cache (p,8) = 30
    p[0] = 99;      // write-through (p,0)
    int c = p[0];   // should be 99
    int d = p[2];   // (p,8) untouched → still 30
    return (a == 10 && b == 30 && c == 99 && d == 30) ? 1 : 0;
}

static void test_const_store_updates_entry(void) {
    int data[4] = {10, 20, 30, 40};
    if (!const_store_write_through(data))
        __builtin_trap();
    result++;
}

// ------------------------------------------------------------------
// Constant store to a DIFFERENT offset does NOT invalidate other offset
// ------------------------------------------------------------------
static int const_store_other_offset(int *restrict p) {
    int a = p[0];   // cache (p,0) = 5
    p[1] = 100;     // write-through (p,4); (p,0) entry must be unaffected
    int b = p[0];   // hit: should still be 5
    return (a == 5 && b == 5) ? 1 : 0;
}

static void test_const_store_other_offset_no_invalidate(void) {
    int data[4] = {5, 6, 7, 8};
    if (!const_store_other_offset(data))
        __builtin_trap();
    result++;
}

// ------------------------------------------------------------------
// Variable-index store invalidates ALL cached offsets for that param
// ------------------------------------------------------------------
static int variable_index_invalidates(int *restrict p, int i) {
    int a = p[0];   // cache (p,0)
    int b = p[1];   // cache (p,4)
    p[i] = 99;      // variable index: invalidates all p entries
    int c = p[0];   // must reload (i==0 → data[0]==99)
    int d = p[1];   // must reload (data[1] unchanged)
    return (a == 10 && b == 20 && c == 99 && d == 20) ? 1 : 0;
}

static void test_variable_index_store_invalidates_all(void) {
    int data[4] = {10, 20, 30, 40};
    if (!variable_index_invalidates(data, 0))
        __builtin_trap();
    result++;
}

// ------------------------------------------------------------------
// char *restrict: width normalization at constant offset
// ------------------------------------------------------------------
static int char_restrict_indexed(signed char *restrict p, int *restrict q) {
    int a = p[1];   // cache (p,1) = 2
    *q = 55;
    int b = p[1];   // hit
    return (a == 2 && b == 2) ? 1 : 0;
}

static void test_char_restrict_indexed_const(void) {
    signed char data[4] = {-1, 2, -3, 4};
    int other = 0;
    if (!char_restrict_indexed(data, &other))
        __builtin_trap();
    result++;
}

// ------------------------------------------------------------------
// Derived pointer store conservatively invalidates all entries
// ------------------------------------------------------------------
static int derived_ptr_invalidates(int *restrict p) {
    int a = p[0];   // cache (p,0)
    int *r = p;     // derived, non-restrict
    *r = 99;        // unknown base → invalidate all
    int b = p[0];   // must reload: 99
    return (a == 10 && b == 99) ? 1 : 0;
}

static void test_derived_pointer_store_invalidates(void) {
    int data[4] = {10, 20, 30, 40};
    if (!derived_ptr_invalidates(data))
        __builtin_trap();
    result++;
}

// ------------------------------------------------------------------
// Plain *p (offset 0) still works identically after the refactor
// ------------------------------------------------------------------
static int plain_deref_still_works(int *restrict p, int *restrict q) {
    int a = *p;     // cache (p,0)
    *q = 99;        // non-aliasing
    int b = *p;     // hit
    return (a == 42 && b == 42) ? 1 : 0;
}

static void test_plain_deref_still_works(void) {
    int x = 42, other = 0;
    if (!plain_deref_still_works(&x, &other))
        __builtin_trap();
    result++;
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
    result++;
}

int main(void) {
    test_indexed_const_no_invalidate_by_q();
    test_two_offsets_coexist();
    test_const_store_updates_entry();
    test_const_store_other_offset_no_invalidate();
    test_variable_index_store_invalidates_all();
    test_char_restrict_indexed_const();
    test_derived_pointer_store_invalidates();
    test_plain_deref_still_works();
    test_array_param_restrict_decay();
    if (result != 9)
        return 1;
    return 42;
}
