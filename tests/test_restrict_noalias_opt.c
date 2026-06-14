// Test #267: restrict deref cache — store through *q must not invalidate *p cache.
// Both p and q are restrict-qualified scalar pointer params (different objects).

static int result = 0;

// Read *p, write *q, read *p again: the second read should use the cached value.
static int noalias_test(int *restrict p, int *restrict q) {
    int a = *p;   // load *p, cache it
    *q = 99;      // write to *q — must NOT invalidate p's cache
    int b = *p;   // should use cached value (same as a)
    return a + b; // == 2 * (*p original)
}

// Verify that the restrict contract is upheld: p and q point to different objects.
static void test_basic_noalias(void) {
    int x = 21, y = 0;
    int r = noalias_test(&x, &y);
    // a == 21, b == 21 (p wasn't modified through q), so r == 42
    if (r != 42)
        __builtin_trap();
    if (y != 99)
        __builtin_trap();
    result++;
}

// Multiple restrict params: p, q, r — stores through q and r must not
// invalidate p's cached value.
static int three_restrict(int *restrict p, int *restrict q, int *restrict r) {
    int a = *p;
    *q = 1;
    *r = 2;
    int b = *p; // should still be the original value of *p
    return b - a; // should be 0
}

static void test_three_params(void) {
    int x = 10, y = 0, z = 0;
    int r = three_restrict(&x, &y, &z);
    if (r != 0)
        __builtin_trap();
    if (y != 1 || z != 2)
        __builtin_trap();
    result++;
}

// A write through *p itself must update the cache (not leave stale value).
static int write_through_p(int *restrict p, int *restrict q) {
    int a = *p;   // cache *p = a
    *p = a + 1;   // write new value to *p
    *q = 99;      // unrelated write
    int b = *p;   // must see a+1, not the stale cached a
    return b;
}

static void test_write_through_p(void) {
    int x = 5, y = 0;
    int r = write_through_p(&x, &y);
    if (r != 6)
        __builtin_trap();
    if (x != 6)
        __builtin_trap();
    result++;
}

// Restrict on char pointers.
static int char_restrict(char *restrict p, char *restrict q) {
    char a = *p;
    *q = 'X';
    char b = *p;
    return (b == a) ? 1 : 0;
}

static void test_char_restrict(void) {
    char x = 'A', y = 0;
    if (!char_restrict(&x, &y))
        __builtin_trap();
    if (y != 'X')
        __builtin_trap();
    result++;
}

// A pointer derived from a restrict param is still "based on" that param (C11 6.7.3.1).
// Storing through the derived pointer must update / invalidate the restrict cache.
static int derived_ptr_store(int *restrict p, int *restrict q) {
    (void)q;
    int a = *p;    // cache *p = a
    int *r = p;    // r is based on p
    *r = a + 10;   // modifies *p through the derived pointer; must not leave stale cache
    return *p;     // must return a+10, not the stale cached a
}

static void test_derived_ptr_store(void) {
    int x = 5, y = 0;
    int r = derived_ptr_store(&x, &y);
    // a=5, *p set to 15 through r, so *p must return 15
    if (r != 15)
        __builtin_trap();
    if (x != 15)
        __builtin_trap();
    result++;
}

// Storing a wide value through a char *restrict must truncate the cache entry.
// STR_B only writes the low byte; the cache must reflect the truncated value.
static int char_store_truncation(char *restrict p, int wide) {
    *p = (char)wide;  // wide may have bits above 0xFF; STR_B stores low byte only
    int b = *p;       // must return sign/zero-extended low byte, not the raw wide value
    return b;
}

static void test_char_store_truncation(void) {
    char x = 0;
    // 0x141 truncated to char (signed) is 0x41 = 65
    int r = char_store_truncation(&x, 0x141);
    if (r != (char)0x141)  // == 65
        __builtin_trap();
    if (x != (char)0x141)
        __builtin_trap();
    result++;
}

int main(void) {
    test_basic_noalias();
    test_three_params();
    test_write_through_p();
    test_char_restrict();
    test_derived_ptr_store();
    test_char_store_truncation();
    // All 6 subtests passed
    if (result != 6)
        return 1;
    return 42;
}
