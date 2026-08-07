// CCCC_FLAGS: --testing --checked-pointers
// Checked C-style spatial-safety layer (#770/#482/#483/#484): checked
// pointer types ([[cccc::single/array/ntarray]]), bounds declarations
// (count()/byte_count()/bounds()), and their CHKR runtime lowering.
// Positive cases and runtime-trap cases live here (single compiling file,
// --checked-pointers on for the whole suite); compile-error cases,
// prototype-only declarations, the opt-in-by-default proof and the
// -O2/-O3 fusion proof cannot share a compiling file with these and live
// in standalone tests/test_checked_pointers_*.c files instead.

#include <stdlib.h>

// ---------------------------------------------------------------------
// count() -- locals, globals, params; heap, stack and global storage.
// ---------------------------------------------------------------------

static int g_n = 4;
static int * [[cccc::array, cccc::count(g_n)]] g_arr = (int[4]){10, 20, 30, 40};

[[cccc::test]]
void test_count_global_array_in_bounds(void) {
    AssertEq(g_arr[0], 10);
    AssertEq(g_arr[3], 40);
}

[[cccc::test(exit_code = 255)]]
void test_count_global_array_oob(void) {
    volatile int i = 4;
    int x = g_arr[i];
    (void)x;
}

[[cccc::test]]
void test_count_stack_array_in_bounds(void) {
    int n = 5;
    int * [[cccc::array, cccc::count(n)]] a = (int[5]){1, 2, 3, 4, 5};
    AssertEq(a[0], 1);
    AssertEq(a[4], 5);
}

// This is precisely the case plain CHKB cannot do (#770): a stack array has
// no AllocHeader, so CHKB has no upper bound for it at all. CHKR's bound
// comes from the declaration, not from allocation metadata, so it catches
// this.
[[cccc::test(exit_code = 255)]]
void test_count_stack_array_oob(void) {
    int n = 5;
    int * [[cccc::array, cccc::count(n)]] a = (int[5]){1, 2, 3, 4, 5};
    volatile int i = 5;
    int x = a[i];
    (void)x;
}

[[cccc::test(exit_code = 255)]]
void test_count_stack_array_negative_index(void) {
    int n = 5;
    int * [[cccc::array, cccc::count(n)]] a = (int[5]){1, 2, 3, 4, 5};
    volatile int i = -1;
    int x = a[i];
    (void)x;
}

[[cccc::test]]
void test_count_heap_array_in_bounds(void) {
    int n = 3;
    int * [[cccc::array, cccc::count(n)]] a = malloc(n * sizeof(int));
    a[0] = 1;
    a[1] = 2;
    a[2] = 3;
    AssertEq(a[0] + a[1] + a[2], 6);
    free(a);
}

[[cccc::test(exit_code = 255)]]
void test_count_heap_array_oob(void) {
    int n = 3;
    int * [[cccc::array, cccc::count(n)]] a = malloc(n * sizeof(int));
    volatile int i = 3;
    a[i] = 99; // still inside the real malloc'd chunk's rounding slack on
               // most allocators -- CHKR must trap on the *declared* count,
               // not the allocator's actual usable size.
    free(a);
}

static void count_param_fill(int * [[cccc::array, cccc::count(n)]] p, int n) {
    for (int i = 0; i < n; i++)
        p[i] = i * i;
}

[[cccc::test]]
void test_count_param_in_bounds(void) {
    int buf[4];
    count_param_fill(buf, 4);
    AssertEq(buf[0], 0);
    AssertEq(buf[3], 9);
}

static int count_param_read_oob(int * [[cccc::array, cccc::count(n)]] p, int n,
                                int idx) {
    return p[idx];
}

[[cccc::test(exit_code = 255)]]
void test_count_param_oob(void) {
    int buf[4] = {1, 2, 3, 4};
    volatile int idx = 4;
    int x = count_param_read_oob(buf, 4, idx);
    (void)x;
}

// ---------------------------------------------------------------------
// byte_count()
// ---------------------------------------------------------------------

[[cccc::test]]
void test_byte_count_in_bounds(void) {
    int nbytes = 4 * (int)sizeof(int);
    char * [[cccc::array, cccc::byte_count(nbytes)]] b =
        (char *)(int[4]){1, 2, 3, 4};
    AssertEq(b[0], 1);
}

[[cccc::test(exit_code = 255)]]
void test_byte_count_oob(void) {
    int nbytes = 4 * (int)sizeof(int);
    char * [[cccc::array, cccc::byte_count(nbytes)]] b =
        (char *)(int[4]){1, 2, 3, 4};
    volatile int i = nbytes;
    char x = b[i];
    (void)x;
}

// ---------------------------------------------------------------------
// bounds(lo, hi) -- explicit range, and bounds(unknown)
// ---------------------------------------------------------------------

[[cccc::test]]
void test_bounds_range_in_bounds(void) {
    int arr[6] = {1, 2, 3, 4, 5, 6};
    int * [[cccc::array, cccc::bounds(arr, arr + 6)]] a = arr;
    AssertEq(a[0], 1);
    AssertEq(a[5], 6);
}

[[cccc::test(exit_code = 255)]]
void test_bounds_range_oob_high(void) {
    int arr[6] = {1, 2, 3, 4, 5, 6};
    int * [[cccc::array, cccc::bounds(arr, arr + 6)]] a = arr;
    volatile int i = 6;
    int x = a[i];
    (void)x;
}

[[cccc::test(exit_code = 255)]]
void test_bounds_range_oob_low(void) {
    int arr[6] = {1, 2, 3, 4, 5, 6};
    // Narrower than the underlying array -- CHKR enforces the DECLARED
    // range, not the storage's real extent.
    int * [[cccc::array, cccc::bounds(arr + 1, arr + 6)]] a = arr + 1;
    volatile int i = -1;
    int x = a[i];
    (void)x;
}

// bounds(unknown) is the trust escape hatch: the type is checked-array, but
// no runtime range check is ever emitted for it.
[[cccc::test]]
void test_bounds_unknown_no_check(void) {
    int arr[3] = {7, 8, 9};
    int * [[cccc::array, cccc::bounds(unknown)]] a = arr;
    AssertEq(a[0], 7);
    AssertEq(a[2], 9);
}

// ---------------------------------------------------------------------
// [[cccc::single]] -- implicit [p, p+sizeof(T)) range, NULL-checked deref
// ---------------------------------------------------------------------

[[cccc::test]]
void test_single_deref_ok(void) {
    int x = 99;
    int * [[cccc::single]] p = &x;
    AssertEq(*p, 99);
}

[[cccc::test(exit_code = 255)]]
void test_single_null_deref_traps(void) {
    int * [[cccc::single]] p = 0;
    int x = *p;
    (void)x;
}

// ---------------------------------------------------------------------
// Interior pointer / pointer arithmetic within a single expression:
// bounds carry through (p+k)[i] but are still checked against p's own
// declared range (#484's "carries within an expression, not across
// assignment" semantics).
// ---------------------------------------------------------------------

[[cccc::test]]
void test_interior_pointer_in_bounds(void) {
    int n = 6;
    int * [[cccc::array, cccc::count(n)]] p = (int[6]){0, 1, 2, 3, 4, 5};
    AssertEq((p + 2)[3], 5); // p[5], last valid element
}

[[cccc::test(exit_code = 255)]]
void test_interior_pointer_oob(void) {
    int n = 6;
    int * [[cccc::array, cccc::count(n)]] p = (int[6]){0, 1, 2, 3, 4, 5};
    volatile int k = 2, i = 4;
    int x = (p + k)[i]; // p[6], one past the declared count
    (void)x;
}

// ---------------------------------------------------------------------
// [[cccc::ntarray]] -- like array, but count(n) widens by one element for
// the terminator slot, and writing that slot is permitted (#483).
// ---------------------------------------------------------------------

[[cccc::test]]
void test_ntarray_terminator_write(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    AssertEq(s[0], 'a');
    s[3] = '\0'; // the terminator slot -- one past count(n), still in range
    AssertEq(s[3], 0);
}

[[cccc::test(exit_code = 255)]]
void test_ntarray_past_terminator_traps(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    volatile int i = 4; // one past the terminator slot
    char x = s[i];
    (void)x;
}

// ---------------------------------------------------------------------
// Both the load path (x = p[i]) and the store path (p[i] = x) must trap.
// ---------------------------------------------------------------------

[[cccc::test(exit_code = 255)]]
void test_load_path_traps(void) {
    int n = 2;
    int * [[cccc::array, cccc::count(n)]] a = (int[2]){1, 2};
    volatile int i = 2;
    int x = a[i];
    (void)x;
}

[[cccc::test(exit_code = 255)]]
void test_store_path_traps(void) {
    int n = 2;
    int * [[cccc::array, cccc::count(n)]] a = (int[2]){1, 2};
    volatile int i = 2;
    a[i] = 5;
}

// Read-modify-write (`a[i] += 1`, `a[i]++`) desugars through to_assign()
// into ND_DEREF(ND_VAR tmp) for the actual load/store, where `tmp = &a[i]`
// -- the address-of builds an ND_ADDR over the original postfix ND_DEREF,
// so gen_addr's CHKR check still fires there. Verified, not assumed.
[[cccc::test(exit_code = 255)]]
void test_compound_assign_traps(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] a = (int[4]){1, 2, 3, 4};
    volatile int i = 4;
    a[i] += 1;
}

[[cccc::test(exit_code = 255)]]
void test_increment_traps(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] a = (int[4]){1, 2, 3, 4};
    volatile int i = 4;
    a[i]++;
}

// ---------------------------------------------------------------------
// p->x is short for (*p).x -- both spellings must be checked identically.
// ---------------------------------------------------------------------

struct checked_pointers_s { int x; };

[[cccc::test]]
void test_arrow_deref_ok(void) {
    struct checked_pointers_s s = {7};
    struct checked_pointers_s * [[cccc::single]] p = &s;
    AssertEq(p->x, 7);
}

[[cccc::test(exit_code = 255)]]
void test_arrow_null_deref_traps(void) {
    struct checked_pointers_s * [[cccc::single]] p = 0;
    int x = p->x;
    (void)x;
}
