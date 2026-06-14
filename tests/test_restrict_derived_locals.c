// Test #269: restrict derived-local alias analysis.
// Locals provably derived from restrict params (q = p + 1) inherit the
// non-aliasing property so the deref cache can treat *q like p[1].

static int result = 0;

// Basic derived read: *q should be cached as (p, sizeof(int)).
// After storing to *p, the cache for (p, 0) is updated but q's slot is unaffected.
static int derived_basic_read(int *restrict p) {
    int *q = p + 1;
    int a = *q;   // loads p[1], caches at (p, sizeof(int))
    *p = 10;      // writes p[0], only invalidates (p,0) slot
    int b = *q;   // should still see p[1] from cache
    return a + b;
}

static void test_derived_basic_read(void) {
    int arr[2] = {0, 7};
    int r = derived_basic_read(arr);
    if (r != 14)  // a==7, b==7 (p[1] unchanged)
        __builtin_trap();
    if (arr[0] != 10)
        __builtin_trap();
    result++;
}

// Write-through via derived local: store through *q must update cache at (p, 4).
static int derived_write_through(int *restrict p) {
    int *q = p + 1;
    int a = p[1];    // load p[1], cache (p, sizeof(int)) = 7
    *q = 99;         // store through derived local: must write-through cache slot
    return p[1];     // must see 99, not stale 7
}

static void test_derived_write_through(void) {
    int arr[2] = {0, 7};
    int r = derived_write_through(arr);
    if (r != 99)
        __builtin_trap();
    result++;
}

// No over-invalidation: store through derived q must not touch s's cache slot.
static int derived_no_overinvalidate(int *restrict p, int *restrict s) {
    int *q = p + 1;
    int a = *s;      // cache *s
    *q = 99;         // store via derived q → invalidates only p's slots, not s's
    int b = *s;      // must still see cached *s value
    return a + b;    // == 2 * original *s
}

static void test_derived_no_overinvalidate(void) {
    int x = 5, y = 0;
    int arr[2] = {0, 0};
    int r = derived_no_overinvalidate(arr, &x);
    if (r != 10)  // a==5, b==5
        __builtin_trap();
    result++;
}

// Negative offset: q = p - 1 (pointing one element before p).
static int derived_negative_offset(int *restrict p) {
    int *q = p - 1;
    int a = *q;   // reads p[-1]
    return a;
}

static void test_derived_negative_offset(void) {
    int arr[3] = {3, 7, 0};
    // Pass &arr[1] so q = &arr[0]
    int r = derived_negative_offset(&arr[1]);
    if (r != 3)
        __builtin_trap();
    result++;
}

// Bail case — variable offset: q = p + n. q must not be cached but store
// through *q must still invalidate p's slots (not do a global invalidate).
static int derived_variable_offset(int *restrict p, int *restrict s, int n) {
    int *q = p + n;
    int a = *s;      // cache *s
    *q = 99;         // store via unknown-offset derived ptr → invalidate p's slots only
    int b = *s;      // s's cache must be intact
    return a + b;
}

static void test_derived_variable_offset(void) {
    int x = 5;
    int arr[4] = {0, 0, 0, 0};
    int r = derived_variable_offset(arr, &x, 2);
    if (r != 10)  // a==5, b==5, *s unchanged
        __builtin_trap();
    if (arr[2] != 99)
        __builtin_trap();
    result++;
}

// Bail case — address of q taken: q not tracked, reads go through normal path.
static int derived_addr_taken(int *restrict p) {
    int *q = p + 1;
    int **r = &q;   // q's address is taken; q must not be in the derivation map
    int a = *q;
    *p = 0;         // modifies p[0], not p[1]
    return a + **r; // a and **r both == p[1], result == 2 * p[1]
}

static void test_derived_addr_taken(void) {
    int arr[2] = {0, 21};
    int r = derived_addr_taken(arr);
    if (r != 42)
        __builtin_trap();
    result++;
}

// Bail case — multiple assignments to q: q not tracked.
static int derived_multi_assign(int *restrict p) {
    int *q = p + 1;
    q = p + 2;    // second assignment → q is not tracked
    return *q;    // reads p[2]; normal load path
}

static void test_derived_multi_assign(void) {
    int arr[3] = {0, 7, 42};
    int r = derived_multi_assign(arr);
    if (r != 42)
        __builtin_trap();
    result++;
}

int main(void) {
    test_derived_basic_read();
    test_derived_write_through();
    test_derived_no_overinvalidate();
    test_derived_negative_offset();
    test_derived_variable_offset();
    test_derived_addr_taken();
    test_derived_multi_assign();
    if (result != 7)
        return 1;
    return 42;
}
