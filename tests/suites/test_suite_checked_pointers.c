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
// #923 -- CHKNT: the terminator slot must stay null. Writing '\0' there is
// the whole point of the widening (test_ntarray_terminator_write above
// already covers that this doesn't over-trap); writing anything else
// destroys the nt invariant within the pointer's own declared extent and
// traps, even though the address itself is in-range for CHKR.
// ---------------------------------------------------------------------

[[cccc::test(exit_code = 255)]]
void test_ntarray_terminator_nonnull_traps(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    s[3] = 'x'; // terminator slot, non-null -- traps
}

[[cccc::test(exit_code = 255)]]
void test_ntarray_terminator_nonnull_traps_runtime_index(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    volatile int i = 3; // same slot, but via a runtime (non-constant) index
    s[i] = 'x';
}

[[cccc::test]]
void test_ntarray_inside_count_nonnull_ok(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    s[n - 1] = 'z'; // still inside count(n), not the terminator slot
    AssertEq(s[n - 1], 'z');
}

[[cccc::test]]
void test_ntarray_int_terminator_zero_ok(void) {
    int n = 3;
    int * [[cccc::ntarray, cccc::count(n)]] a = (int[4]){1, 2, 3, 0};
    a[n] = 0; // terminator slot, null -- proves hi - elem_size for sizeof > 1
    AssertEq(a[n], 0);
}

[[cccc::test(exit_code = 255)]]
void test_ntarray_int_terminator_nonzero_traps(void) {
    int n = 3;
    int * [[cccc::ntarray, cccc::count(n)]] a = (int[4]){1, 2, 3, 0};
    a[n] = 1; // terminator slot, non-null -- traps
}

// A plain [[cccc::array]] (no ntarray) gets no widening at all, so a write
// one past count(n) is an ordinary CHKR out-of-bounds trap, unaffected by
// the CHKNT guard added for ntarray.
[[cccc::test(exit_code = 255)]]
void test_array_no_widening_oob_write_traps(void) {
    int n = 3;
    char * [[cccc::array, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    s[n] = 0; // one past count(n) -- CHKR OOB, no terminator slot exists
}

// ---------------------------------------------------------------------
// #938 -- byte_count(n)/bounds(lo, hi) on ntarray now get the same
// terminator-slot widening as count(n): the slot is the elem_size bytes
// beginning at the declared end of the range (byte_count's end is `base +
// n`, bounds' end is `hi`), and CHKNT enforces it identically.
// ---------------------------------------------------------------------

[[cccc::test]]
void test_ntarray_byte_count_terminator_write(void) {
    int nbytes = 3;
    char * [[cccc::ntarray, cccc::byte_count(nbytes)]] s =
        (char[4]){'a', 'b', 'c', 0};
    s[nbytes] = '\0'; // the terminator slot -- one past byte_count(n), still in range
    AssertEq(s[nbytes], 0);
}

[[cccc::test(exit_code = 255)]]
void test_ntarray_byte_count_terminator_nonnull_traps(void) {
    int nbytes = 3;
    char * [[cccc::ntarray, cccc::byte_count(nbytes)]] s =
        (char[4]){'a', 'b', 'c', 0};
    s[nbytes] = 'x'; // terminator slot, non-null -- traps
}

[[cccc::test(exit_code = 255)]]
void test_ntarray_byte_count_terminator_nonnull_traps_runtime_index(void) {
    int nbytes = 3;
    char * [[cccc::ntarray, cccc::byte_count(nbytes)]] s =
        (char[4]){'a', 'b', 'c', 0};
    volatile int i = nbytes;
    s[i] = 'x';
}

[[cccc::test]]
void test_ntarray_byte_count_inside_range_nonnull_ok(void) {
    int nbytes = 3;
    char * [[cccc::ntarray, cccc::byte_count(nbytes)]] s =
        (char[4]){'a', 'b', 'c', 0};
    s[nbytes - 1] = 'z'; // still inside byte_count(n), not the terminator slot
    AssertEq(s[nbytes - 1], 'z');
}

[[cccc::test(exit_code = 255)]]
void test_ntarray_byte_count_past_terminator_traps(void) {
    int nbytes = 3;
    char * [[cccc::ntarray, cccc::byte_count(nbytes)]] s =
        (char[4]){'a', 'b', 'c', 0};
    volatile int i = nbytes + 1; // one past the terminator slot
    char x = s[i];
    (void)x;
}

// int pointee (elem_size > 1) -- byte_count is a byte offset, so the
// terminator slot lands at byte offset nbytes / element index nbytes/4,
// proving the widening is byte-granular, not element-granular.
[[cccc::test]]
void test_ntarray_byte_count_int_terminator_zero_ok(void) {
    int nbytes = 3 * (int)sizeof(int);
    int * [[cccc::ntarray, cccc::byte_count(nbytes)]] a =
        (int[4]){1, 2, 3, 0};
    a[3] = 0; // byte offset 12 == element index 3, terminator slot, null
    AssertEq(a[3], 0);
}

[[cccc::test(exit_code = 255)]]
void test_ntarray_byte_count_int_terminator_nonzero_traps(void) {
    int nbytes = 3 * (int)sizeof(int);
    int * [[cccc::ntarray, cccc::byte_count(nbytes)]] a =
        (int[4]){1, 2, 3, 0};
    a[3] = 1; // terminator slot, non-null -- traps
}

[[cccc::test]]
void test_ntarray_bounds_terminator_write(void) {
    char buf[4] = {'a', 'b', 'c', 0};
    char * [[cccc::ntarray, cccc::bounds(buf, buf + 3)]] s = buf;
    s[3] = '\0'; // the terminator slot -- one past declared hi, still in range
    AssertEq(s[3], 0);
}

[[cccc::test(exit_code = 255)]]
void test_ntarray_bounds_terminator_nonnull_traps(void) {
    char buf[4] = {'a', 'b', 'c', 0};
    char * [[cccc::ntarray, cccc::bounds(buf, buf + 3)]] s = buf;
    s[3] = 'x'; // terminator slot, non-null -- traps
}

[[cccc::test(exit_code = 255)]]
void test_ntarray_bounds_terminator_nonnull_traps_runtime_index(void) {
    char buf[4] = {'a', 'b', 'c', 0};
    char * [[cccc::ntarray, cccc::bounds(buf, buf + 3)]] s = buf;
    volatile int i = 3;
    s[i] = 'x';
}

[[cccc::test]]
void test_ntarray_bounds_inside_range_nonnull_ok(void) {
    char buf[4] = {'a', 'b', 'c', 0};
    char * [[cccc::ntarray, cccc::bounds(buf, buf + 3)]] s = buf;
    s[2] = 'z'; // still inside [buf, buf+3), not the terminator slot
    AssertEq(s[2], 'z');
}

[[cccc::test(exit_code = 255)]]
void test_ntarray_bounds_past_terminator_traps(void) {
    char buf[4] = {'a', 'b', 'c', 0};
    char * [[cccc::ntarray, cccc::bounds(buf, buf + 3)]] s = buf;
    volatile int i = 4; // one past the terminator slot
    char x = s[i];
    (void)x;
}

// int pointee (elem_size > 1) via bounds(lo, hi).
[[cccc::test]]
void test_ntarray_bounds_int_terminator_zero_ok(void) {
    int arr[4] = {1, 2, 3, 0};
    int * [[cccc::ntarray, cccc::bounds(arr, arr + 3)]] a = arr;
    a[3] = 0; // terminator slot, null -- proves hi - elem_size for sizeof > 1
    AssertEq(a[3], 0);
}

[[cccc::test(exit_code = 255)]]
void test_ntarray_bounds_int_terminator_nonzero_traps(void) {
    int arr[4] = {1, 2, 3, 0};
    int * [[cccc::ntarray, cccc::bounds(arr, arr + 3)]] a = arr;
    a[3] = 1; // terminator slot, non-null -- traps
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
// so gen_addr's CHKR check still fires there. Verified, not assumed. This
// only proves out-of-bounds CHKR coverage on the RMW path -- it says
// nothing about CHKNT's terminator-slot guard, which (until #937) the RMW
// desugar's synthesized store deref never carried; see the CHKNT RMW block
// below for that coverage.
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
// #937 -- CHKNT on read-modify-write through the terminator slot. `s[n] +=
// 1`/`s[n]++`/`s[n]--` desugar via to_assign() into `tmp = &s[n]; *tmp =
// *tmp op B`; the synthesized `*tmp` store now carries the same
// checked_bounds_lo/hi/checked_nt_terminator as `s[n]` itself, so CHKNT
// covers it exactly as it covers a direct `s[n] = x`.
// ---------------------------------------------------------------------

[[cccc::test(exit_code = 255)]]
void test_ntarray_terminator_rmw_traps(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    s[n] += 1; // null terminator slot -> 1, non-null -- traps
}

[[cccc::test(exit_code = 255)]]
void test_ntarray_terminator_increment_traps(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    s[n]++; // same slot, postfix increment
}

[[cccc::test(exit_code = 255)]]
void test_ntarray_terminator_decrement_traps(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    s[n]--; // null -> -1, non-null -- traps
}

[[cccc::test(exit_code = 255)]]
void test_ntarray_terminator_rmw_runtime_index_traps(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    volatile int i = 3; // same slot, but via a runtime (non-constant) index
    s[i] += 1;
}

[[cccc::test(exit_code = 255)]]
void test_ntarray_int_terminator_rmw_traps(void) {
    int n = 3;
    int * [[cccc::ntarray, cccc::count(n)]] a = (int[4]){1, 2, 3, 0};
    a[n] += 1; // proves hi - elem_size for sizeof > 1 on the RMW path
}

[[cccc::test]]
void test_ntarray_terminator_rmw_zero_ok(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    s[n] += 0; // stays null -- no trap
    AssertEq(s[n], 0);
}

[[cccc::test]]
void test_ntarray_inside_count_rmw_ok(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    s[n - 1] += 1; // still inside count(n), not the terminator slot
    AssertEq(s[n - 1], 'd');
}

// _Atomic ntarray RMW takes to_assign()'s CAS-loop desugar instead of the
// plain ND_ASSIGN one -- a separate code path (ND_CAS in codegen.c) that
// needed its own CHKNT emission.
[[cccc::test(exit_code = 255)]]
void test_ntarray_atomic_terminator_rmw_traps(void) {
    int n = 3;
    _Atomic char backing[4] = {'a', 'b', 'c', 0};
    _Atomic char * [[cccc::ntarray, cccc::count(n)]] s = backing;
    s[n] += 1; // terminator slot, non-null via the CAS-loop store -- traps
}

[[cccc::test]]
void test_ntarray_atomic_terminator_rmw_zero_ok(void) {
    int n = 3;
    _Atomic char backing[4] = {'a', 'b', 'c', 0};
    _Atomic char * [[cccc::ntarray, cccc::count(n)]] s = backing;
    s[n] += 0; // stays null -- no trap
    AssertEq(s[n], 0);
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

// ---------------------------------------------------------------------
// #921 -- checked-pointer bounds on struct/union members. A bounds
// expression may name a sibling field (count(n) resolving to `self->n`
// relative to whatever struct instance is accessed, not a fixed scope --
// see resolve_member_checked_bounds()/compute_checked_bounds() in
// src/parse.c) or a global; compile-error cases (an enclosing local, a
// bit-field sibling, side effects) live in standalone
// tests/test_checked_pointers_member_*_error.c instead, matching the
// existing split for Obj-rooted bounds errors.
// ---------------------------------------------------------------------

struct mem_count_s {
    int n;
    int * [[cccc::array, cccc::count(n)]] p;
};

[[cccc::test]]
void test_member_count_sibling_after_ptr_in_bounds(void) {
    struct mem_count_s s = {4, (int[4]){10, 20, 30, 40}};
    AssertEq(s.p[0], 10);
    AssertEq(s.p[3], 40);
}

[[cccc::test(exit_code = 255)]]
void test_member_count_sibling_after_ptr_oob(void) {
    struct mem_count_s s = {4, (int[4]){10, 20, 30, 40}};
    volatile int i = 4;
    int x = s.p[i];
    (void)x;
}

// Sibling field declared BEFORE the pointer member this time -- proves
// resolve_member_checked_bounds() doesn't depend on textual order any more
// than parameter bounds do (a bound may name a LATER field/param).
struct mem_count_before_s {
    int * [[cccc::array, cccc::count(n)]] p;
    int n;
};

[[cccc::test]]
void test_member_count_sibling_before_ptr_in_bounds(void) {
    struct mem_count_before_s s = {(int[3]){1, 2, 3}, 3};
    AssertEq(s.p[2], 3);
}

[[cccc::test(exit_code = 255)]]
void test_member_count_sibling_before_ptr_oob(void) {
    struct mem_count_before_s s = {(int[3]){1, 2, 3}, 3};
    volatile int i = 3;
    int x = s.p[i];
    (void)x;
}

struct mem_byte_count_s {
    int nbytes;
    char * [[cccc::array, cccc::byte_count(nbytes)]] b;
};

[[cccc::test]]
void test_member_byte_count_in_bounds(void) {
    struct mem_byte_count_s s = {4 * (int)sizeof(int), (char *)(int[4]){1, 2, 3, 4}};
    AssertEq(s.b[0], 1);
}

[[cccc::test(exit_code = 255)]]
void test_member_byte_count_oob(void) {
    struct mem_byte_count_s s = {4 * (int)sizeof(int), (char *)(int[4]){1, 2, 3, 4}};
    volatile int i = s.nbytes;
    char x = s.b[i];
    (void)x;
}

struct mem_range_s {
    int * [[cccc::array, cccc::bounds(arr, arr + 4)]] p;
    int arr[4];
};

[[cccc::test]]
void test_member_bounds_range_in_bounds(void) {
    struct mem_range_s s;
    s.arr[0] = 1; s.arr[1] = 2; s.arr[2] = 3; s.arr[3] = 4;
    s.p = s.arr;
    AssertEq(s.p[0], 1);
    AssertEq(s.p[3], 4);
}

[[cccc::test(exit_code = 255)]]
void test_member_bounds_range_oob(void) {
    struct mem_range_s s;
    s.arr[0] = 1; s.arr[1] = 2; s.arr[2] = 3; s.arr[3] = 4;
    s.p = s.arr;
    volatile int i = 4;
    int x = s.p[i];
    (void)x;
}

struct mem_single_s {
    int * [[cccc::single]] p;
};

[[cccc::test]]
void test_member_single_deref_ok(void) {
    int x = 99;
    struct mem_single_s s = {&x};
    AssertEq(*s.p, 99);
}

[[cccc::test(exit_code = 255)]]
void test_member_single_null_deref_traps(void) {
    struct mem_single_s s = {0};
    int x = *s.p;
    (void)x;
}

// s.p[i], sp->p[i], (&s)->p[i], (*sp).p[i], *s.p -- all reach the same
// member-relative base through find_checked_base()'s ND_ADD/ND_SUB/ND_CAST
// descent, same as the existing Obj-rooted interior-pointer coverage above.
[[cccc::test]]
void test_member_access_spellings_agree(void) {
    struct mem_count_s s = {2, (int[2]){11, 22}};
    struct mem_count_s *sp = &s;
    AssertEq(s.p[1], 22);
    AssertEq(sp->p[1], 22);
    AssertEq((&s)->p[1], 22);
    AssertEq((*sp).p[1], 22);
    AssertEq(*s.p, 11);
}

[[cccc::test(exit_code = 255)]]
void test_member_arrow_spelling_oob(void) {
    struct mem_count_s s = {2, (int[2]){11, 22}};
    struct mem_count_s *sp = &s;
    volatile int i = 2;
    int x = sp->p[i];
    (void)x;
}

// Nested struct: outer.inner.p[i] -- base.obj is the direct object
// expression the member belongs to (`outer.inner`, not `outer`), so this
// exercises find_checked_base() through a real ND_MEMBER chain rather than
// a bare ND_VAR.
struct mem_nested_outer_s {
    struct mem_count_s inner;
};

[[cccc::test]]
void test_member_nested_struct_in_bounds(void) {
    struct mem_nested_outer_s outer = {{3, (int[3]){5, 6, 7}}};
    AssertEq(outer.inner.p[2], 7);
}

[[cccc::test(exit_code = 255)]]
void test_member_nested_struct_oob(void) {
    struct mem_nested_outer_s outer = {{3, (int[3]){5, 6, 7}}};
    volatile int i = 3;
    int x = outer.inner.p[i];
    (void)x;
}

// Array of structs: arr[k].p[i] -- the object expression carries a runtime
// index, so it must be evaluated (and checked against) the same struct
// instance both times it's cloned into lo/hi.
[[cccc::test]]
void test_member_array_of_structs_in_bounds(void) {
    struct mem_count_s arr[2] = {
        {2, (int[2]){1, 2}},
        {2, (int[2]){3, 4}},
    };
    volatile int k = 1;
    AssertEq(arr[k].p[1], 4);
}

[[cccc::test(exit_code = 255)]]
void test_member_array_of_structs_oob(void) {
    struct mem_count_s arr[2] = {
        {2, (int[2]){1, 2}},
        {2, (int[2]){3, 4}},
    };
    volatile int k = 1, i = 2;
    int x = arr[k].p[i];
    (void)x;
}

// sp is itself [[cccc::single]] AND sp->p carries its own member bounds --
// two independent checks fire: the `->` deref itself (NULL/range-of-one
// checked against sp), and the member access's own count(n).
[[cccc::test]]
void test_member_via_single_ptr_in_bounds(void) {
    struct mem_count_s s = {2, (int[2]){7, 8}};
    struct mem_count_s * [[cccc::single]] sp = &s;
    AssertEq(sp->p[1], 8);
}

[[cccc::test(exit_code = 255)]]
void test_member_via_single_ptr_null_traps(void) {
    struct mem_count_s * [[cccc::single]] sp = 0;
    int x = sp->p[0]; // the `sp` deref itself traps before the member bound
                       // would even be evaluated
    (void)x;
}

[[cccc::test(exit_code = 255)]]
void test_member_via_single_ptr_member_oob(void) {
    struct mem_count_s s = {2, (int[2]){7, 8}};
    struct mem_count_s * [[cccc::single]] sp = &s;
    volatile int i = 2;
    int x = sp->p[i]; // sp itself is in range; the member's own count(n) traps
    (void)x;
}

// A bound naming a global (not a sibling) still resolves -- the same
// "any in-scope global" rule Obj-rooted bounds already have.
static int g_mem_n = 3;
struct mem_global_bound_s {
    int * [[cccc::array, cccc::count(g_mem_n)]] p;
};

[[cccc::test]]
void test_member_bounds_global_in_bounds(void) {
    struct mem_global_bound_s s = {(int[3]){1, 2, 3}};
    AssertEq(s.p[2], 3);
}

[[cccc::test(exit_code = 255)]]
void test_member_bounds_global_oob(void) {
    struct mem_global_bound_s s = {(int[3]){1, 2, 3}};
    volatile int i = 3;
    int x = s.p[i];
    (void)x;
}

// Union member bounds: the general struct_members() path is shared between
// struct and union, so a checked-pointer member with a bounds form is legal
// on a union too. A union's members alias the same storage, so this
// deliberately binds against a GLOBAL rather than a sibling field -- a
// sibling-field bound would read back whatever bytes the pointer member
// itself last wrote, which is well-defined C type-punning but not a useful
// bounds value and would make the test's pass/fail depend on the host's
// memory layout.
static int g_mem_union_n = 2;
union mem_union_s {
    int * [[cccc::array, cccc::count(g_mem_union_n)]] p;
    long raw;
};

[[cccc::test]]
void test_member_union_bounds_in_bounds(void) {
    union mem_union_s u;
    u.p = (int[2]){21, 22};
    AssertEq(u.p[1], 22);
}

[[cccc::test(exit_code = 255)]]
void test_member_union_bounds_oob(void) {
    union mem_union_s u;
    u.p = (int[2]){21, 22};
    volatile int i = 2;
    int x = u.p[i];
    (void)x;
}

// Opt-out proof: bounds(unknown) on a member is the same trust escape hatch
// as an Obj-rooted bounds(unknown) -- checked-array type, zero runtime check.
struct mem_unknown_s {
    int * [[cccc::array, cccc::bounds(unknown)]] p;
};

[[cccc::test]]
void test_member_bounds_unknown_no_check(void) {
    struct mem_unknown_s s = {(int[3]){1, 2, 3}};
    AssertEq(s.p[0], 1);
    AssertEq(s.p[2], 3);
}

// ---------------------------------------------------------------------
// #919 -- bounds propagation across assignment. `q = p + k;` (or a plain
// `q = p;`) checks against a SNAPSHOT of `p`'s own absolute bounds taken at
// the assignment, not `q`'s own (nonexistent) declared bounds -- see
// propagate_checked_bounds() in src/parse.c and man/SAFETY.md's "Bounds
// propagation across assignment" section for the whole-function
// "propagatable" rule this implements: a local `q` propagates iff it is
// unchecked, its declaration itself has a checked-rooted initializer, and
// EVERY assignment to it in the function is also checked-rooted (no
// dataflow/join analysis -- straight-line rule, sound under arbitrary
// control flow because the snapshot is refreshed at every qualifying
// store). This is layered on top of, and composes with, #921's member
// bounds: a member is a valid propagation source too.
// ---------------------------------------------------------------------

[[cccc::test]]
void test_prop_interior_assignment_in_bounds(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int *q = p + 0;
    AssertEq(q[0], 1);
    AssertEq(q[3], 4);
}

[[cccc::test(exit_code = 255)]]
void test_prop_interior_assignment_oob(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int *q = p + 0;
    volatile int i = 4;
    int x = q[i];
    (void)x;
}

// q++/q += k preserve the propagated fact: the snapshot is an absolute
// [lo, hi) range, independent of q's current value, so walking q forward
// with plain pointer arithmetic (not a fresh assignment) still checks
// against the same range.
[[cccc::test]]
void test_prop_increment_preserves_fact_in_bounds(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){10, 20, 30, 40};
    int *q = p;
    int total = 0;
    for (int i = 0; i < 4; i++) {
        total += *q;
        q++;
    }
    AssertEq(total, 100);
}

[[cccc::test(exit_code = 255)]]
void test_prop_increment_preserves_fact_oob(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){10, 20, 30, 40};
    int *q = p;
    for (int i = 0; i < 4; i++)
        q++;
    int x = *q; // one past the end -- must still trap after 4 plain q++'s
    (void)x;
}

// A cast doesn't lose the propagated fact; checked_access_size comes from
// the ACCESS site's own type (char, here), not the source's (int).
[[cccc::test]]
void test_prop_cast_in_bounds(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    char *q = (char *)p;
    AssertEq(q[0], 1); // little/big-endian-agnostic: byte 0 of int 1 on any
                        // platform this test matrix runs on is non-zero iff
                        // the low byte is; just check it doesn't trap here.
}

[[cccc::test(exit_code = 255)]]
void test_prop_cast_oob(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    char *q = (char *)p;
    volatile int i = 4 * (int)sizeof(int);
    char x = q[i]; // one byte past the declared count(n)*sizeof(int) range
    (void)x;
}

// Propagation from a bounds(lo, hi) source.
[[cccc::test]]
void test_prop_from_bounds_range_in_bounds(void) {
    int arr[4] = {1, 2, 3, 4};
    int * [[cccc::array, cccc::bounds(arr, arr + 4)]] p = arr;
    int *q = p;
    AssertEq(q[3], 4);
}

[[cccc::test(exit_code = 255)]]
void test_prop_from_bounds_range_oob(void) {
    int arr[4] = {1, 2, 3, 4};
    int * [[cccc::array, cccc::bounds(arr, arr + 4)]] p = arr;
    int *q = p;
    volatile int i = 4;
    int x = q[i];
    (void)x;
}

// Propagation from a byte_count() source.
[[cccc::test]]
void test_prop_from_byte_count_in_bounds(void) {
    int nbytes = 4 * (int)sizeof(int);
    char * [[cccc::array, cccc::byte_count(nbytes)]] p =
        (char *)(int[4]){1, 2, 3, 4};
    char *q = p;
    AssertEq(q[0], 1);
}

[[cccc::test(exit_code = 255)]]
void test_prop_from_byte_count_oob(void) {
    int nbytes = 4 * (int)sizeof(int);
    char * [[cccc::array, cccc::byte_count(nbytes)]] p =
        (char *)(int[4]){1, 2, 3, 4};
    char *q = p;
    volatile int i = nbytes;
    char x = q[i];
    (void)x;
}

// Propagation from a #921 struct-member source -- the two features compose.
struct prop_member_s {
    int n;
    int * [[cccc::array, cccc::count(n)]] p;
};

[[cccc::test]]
void test_prop_from_member_source_in_bounds(void) {
    struct prop_member_s s = {4, (int[4]){1, 2, 3, 4}};
    int *q = s.p;
    AssertEq(q[3], 4);
}

[[cccc::test(exit_code = 255)]]
void test_prop_from_member_source_oob(void) {
    struct prop_member_s s = {4, (int[4]){1, 2, 3, 4}};
    int *q = s.p;
    volatile int i = 4;
    int x = q[i];
    (void)x;
}

// Reassignment mid-function re-snapshots: a later access uses the NEW
// range, not the one captured at declaration.
[[cccc::test]]
void test_prop_reassignment_uses_new_range_in_bounds(void) {
    int n1 = 2, n2 = 4;
    int * [[cccc::array, cccc::count(n1)]] p1 = (int[2]){1, 2};
    int * [[cccc::array, cccc::count(n2)]] p2 = (int[4]){9, 8, 7, 6};
    int *q = p1;
    q = p2 + 1; // re-snapshot: now [p2+1, p2+4)
    AssertEq(q[2], 6); // p2[3], last valid element through the new range
}

[[cccc::test(exit_code = 255)]]
void test_prop_reassignment_uses_new_range_oob(void) {
    int n1 = 2, n2 = 4;
    int * [[cccc::array, cccc::count(n1)]] p1 = (int[2]){1, 2};
    int * [[cccc::array, cccc::count(n2)]] p2 = (int[4]){9, 8, 7, 6};
    int *q = p1;
    q = p2 + 1;
    volatile int i = 3; // p2[4] -- one past the re-snapshotted range
    int x = q[i];
    (void)x;
}

// Negative-coverage: proves no false positives. Paired in this same file
// with the positive OOB-traps tests above for the same shape -- if
// propagation were entirely dead, these three would also (wrongly) "pass",
// so the positive traps are what actually prove the feature works.
[[cccc::test]]
void test_prop_no_init_not_propagated(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int *r; // no checked-rooted initializer -- not a candidate
    r = p;
    volatile int i = 10; // past count(n), but r is never checked
    int x = r[i];
    (void)x;
}

[[cccc::test]]
void test_prop_non_checked_rooted_reassign_not_propagated(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int local[4] = {0, 0, 0, 0};
    int *q = local; // checked-rooted?  local is a plain array, not a
                     // checked pointer -- NOT checked-rooted, so q is never
                     // a candidate at all (poisoned right at its own
                     // declaration, see checked_prop_poison_scan()).
    q = p;
    volatile int i = 10;
    int x = q[i];
    (void)x;
}

[[cccc::test]]
void test_prop_address_taken_not_propagated(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int other[2] = {9, 9};
    int *q = p;
    int **pp = &q; // escapes -- poisons q
    *pp = other;
    volatile int i = 10;
    int x = q[i];
    (void)x;
}

// #943: CHKNT now propagates -- a non-null write to the widened terminator
// slot through a propagated ntarray pointer traps exactly like the
// direct-access case (test_ntarray_terminator_nonnull_traps above), because
// propagate_checked_bounds() carries the terminator-slot fact
// (Obj.checked_prop_nt_elem) alongside the snapshot lo/hi it already
// propagated since #919.
[[cccc::test(exit_code = 255)]]
void test_prop_chknt_propagates(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    char *q = s;
    q[3] = 'x'; // now traps via CHKNT through `q`, same as through `s` directly
}

// #943: the null write that legally occupies the terminator slot must still
// be allowed through a propagated pointer, exactly as it is directly.
[[cccc::test]]
void test_prop_chknt_null_write_ok(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    char *q = s;
    q[3] = 0; // still null -- must not trap
    AssertEq(q[3], 0);
}

// #943: a cast that changes the pointee's element size must not misapply the
// ntarray source's terminator-slot fact -- CHKNT's hi - elem_size math would
// no longer point at the real terminator slot for a wider/narrower access
// size than the source's own pointee.
[[cccc::test]]
void test_prop_chknt_elem_size_mismatch_no_guard(void) {
    int n = 7; // char ntarray, widened hi = s + 8
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[8]){1, 2, 3, 4, 5, 6, 7, 0};
    short *q = (short *)s;
    volatile int i = 3;
    // addr = s + 6 = hi - 2 (the short access size, NOT the source's own
    // char element size 1) -- if the element-size guard were missing or
    // wrong, this would be misidentified as the terminator slot and
    // falsely trap; the REAL terminator slot is the single byte at s + 7,
    // which this short-sized store doesn't even touch.
    q[i] = 0x0101;
    AssertEq(q[i], 0x0101);
}

// #943: a candidate with one ntarray-rooted store and one plain-array-rooted
// store must not attach CHKNT at all -- on the array-rooted path, `hi` is
// not widened, so `hi - elem_size` is the LAST REAL ELEMENT, not a
// terminator slot; guarding it would falsely trap a legitimate write there.
[[cccc::test]]
void test_prop_chknt_mixed_source_no_guard(void) {
    int n = 3;
    volatile int c = 0;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    char * [[cccc::array, cccc::count(8)]] a = (char[8]){0};
    char *q;
    if (c)
        q = s;
    else
        q = a;
    q[3] = 'x'; // reached only via the array-rooted path -- must not trap
    AssertEq(q[3], 'x');
}

// #943: same mixed-source conflict as above, but with the NON-ntarray-rooted
// store textually BEFORE the ntarray-rooted one -- checked_prop_poison_scan()
// visits both branches of an `if` every round regardless of which one a
// given run actually takes, so both stores are seen. Without
// Obj.checked_prop_scan_saw_non_nt tracking this direction explicitly, a
// non-ntarray store seen before any ntarray-rooted one would leave
// checked_prop_scan_nt_elem at 0 and be silently overwritten by the LATER
// ntarray store's element size instead of flagging a conflict. This test
// takes the array-rooted branch (declared textually first, taken at
// runtime) and writes to the array's own last valid byte -- under that
// ordering bug, `a`'s hi minus the wrongly-inherited element size (1, from
// `s`) would misidentify this legitimate write as landing on a nonexistent
// terminator slot and falsely trap.
[[cccc::test]]
void test_prop_chknt_mixed_source_reverse_order_no_guard(void) {
    int n = 3;
    volatile int c = 1;
    char * [[cccc::array, cccc::count(8)]] a = (char[8]){0};
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    char *q;
    if (c)
        q = a; // array-rooted store, visited FIRST by the poison scan
    else
        q = s; // ntarray-rooted store, visited SECOND
    q[7] = 'x'; // c is true -- q holds `a`; a's own last valid byte
    AssertEq(q[7], 'x');
}

// #943: NT-ness composes through a #941 chain, same as lo/hi -- a two-hop
// derivation from an ntarray source still traps on a non-null terminator
// write through the final hop.
[[cccc::test(exit_code = 255)]]
void test_prop_chknt_chain_propagates(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    char *q = s + 0;
    char *r = q + 0;
    r[3] = 'x'; // traps via the chained NT fact
}

// ---------------------------------------------------------------------
// #941 -- fixpoint over derived-from-derived checked pointers. #919 only
// let a DECLARED checked pointer/member act as a propagation source; a
// local that was itself only propagated (never declared checked) could
// never be a further source, so enforcement stopped after one hop:
// `int *r = q + 1;` never propagated even when `q` itself propagated from a
// real checked pointer `p`. propagate_checked_bounds() now iterates the
// #919 poison scan to a fixpoint (Obj.checked_prop_chain_src), seeded from
// declared-checked sources only and growing round over round, so trust
// flows down an arbitrarily long derivation chain -- see
// man/SAFETY.md's "Bounds propagation across assignment" section.
// ---------------------------------------------------------------------

[[cccc::test]]
void test_prop_chain_three_deep_in_bounds(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int *q = p + 0;
    int *r = q + 1;
    int *s = r + 1;
    AssertEq(s[1], 4); // p[3], last valid element through the 3-hop chain
}

[[cccc::test(exit_code = 255)]]
void test_prop_chain_three_deep_oob(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int *q = p + 0;
    int *r = q + 1;
    int *s = r + 1;
    volatile int i = 2; // p[4] -- one past the end, through the 3-hop chain
    int x = s[i];
    (void)x;
}

// A chain may root at a #921 member source instead of a declared local --
// the two features compose transitively, not just at the first hop.
[[cccc::test]]
void test_prop_chain_from_member_source_in_bounds(void) {
    struct prop_member_s s = {4, (int[4]){1, 2, 3, 4}};
    int *q = s.p;
    int *r = q + 1;
    AssertEq(r[2], 4); // s.p[3]
}

[[cccc::test(exit_code = 255)]]
void test_prop_chain_from_member_source_oob(void) {
    struct prop_member_s s = {4, (int[4]){1, 2, 3, 4}};
    int *q = s.p;
    int *r = q + 1;
    volatile int i = 3; // s.p[4] -- one past the end
    int x = r[i];
    (void)x;
}

// A broken mid-chain link poisons everything downstream of it -- proves the
// fixpoint isn't over-eager (a naive "assume everyone propagates" pass
// would wrongly let `s` inherit `p`'s range here).
[[cccc::test]]
void test_prop_chain_broken_link_not_propagated(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int local[4] = {0, 0, 0, 0};
    int *q = p + 0;
    q = local; // non-checked-rooted reassignment -- poisons q
    int *r = q + 1; // must NOT propagate: q is poisoned, not a chain source
    volatile int i = 10;
    int x = r[i];
    (void)x;
}

// A cycle with no declared root anywhere in it never validates itself, no
// matter how many fixpoint rounds run -- proves the fixpoint is seeded from
// below (declared sources only) rather than optimistically from above
// (assume everything propagates, then remove failures), which is what
// would let this pair mutually "prove" each other.
[[cccc::test]]
void test_prop_chain_cycle_not_propagated(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int *q = p + 0; // q's OWN declaration is checked-rooted (from p) --
                     // but the reassignment below poisons q for the whole
                     // function (straight-line rule: EVERY assignment to q
                     // must be checked-rooted, not just the declaration),
                     // so q never actually becomes a chain source at all.
    int *r = q + 1; // r's declaration would need q as a chain source to
                     // propagate -- q never qualifies (see above), so r is
                     // poisoned right at its own declaration, same as any
                     // other candidate whose init isn't checked-rooted.
    q = r + 1;      // NOT checked-rooted (r never propagates, see above) --
                     // this is what actually poisons q; included to show
                     // the mutual, no-declared-root shape the comment above
                     // describes, not because q was ever otherwise safe.
    volatile int i = 10;
    int x = q[i]; // never checked once q is poisoned
    (void)x;
}

// #941: a self-rooted reassignment (`q = q + 1;`) is neutral -- it can
// neither poison nor re-root `q`, since the snapshotted range is absolute
// and a self-store can't change it (the same fact that already lets plain
// `q++`/`q += k` preserve propagation).
[[cccc::test]]
void test_prop_self_assign_in_bounds(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int *q = p + 0;
    q = q + 1; // self-rooted -- neutral, not a poison
    AssertEq(q[2], 4); // p[3], through the still-live snapshot
}

[[cccc::test(exit_code = 255)]]
void test_prop_self_assign_oob(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int *q = p + 0;
    q = q + 1; // self-rooted -- neutral
    volatile int i = 3; // p[4] -- one past the end
    int x = q[i];
    (void)x;
}

// The neutral rule is strictly about REASSIGNMENT, not the declaration
// itself: `int *q = q;` has no prior value of `q` to have been rooted in
// anything, so the declaration's own init-assignment must still poison
// rather than being treated as a no-op self-reference (see
// checked_prop_poison_scan()'s `node != checked_prop_init_assign` carve-out
// in src/parse.c). `q`'s value is indeterminate here (self-referential
// initializer, syntactically legal but the read value is UB to dereference)
// -- this test only proves the pass itself handles the shape without
// mis-propagating or crashing the compiler; it never dereferences `q`.
[[cccc::test]]
void test_prop_self_assign_init_not_propagated(void) {
    int *q = q;
    AssertEq(1, 1); // reaches here without a false CHKR trap or a compiler crash
}

// Chained store on a loop back edge: the fixpoint's straight-line,
// no-path-sensitivity rule (inherited from #919) still requires the
// assignment to be checked-rooted on every textual occurrence, so a chain
// re-snapshotted every iteration keeps enforcing.
[[cccc::test]]
void test_prop_chain_in_loop_in_bounds(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){10, 20, 30, 40};
    int *q = p;
    int total = 0;
    for (int i = 0; i < 4; i++) {
        int *r = q; // re-snapshots each iteration
        total += *r;
        q++;
    }
    AssertEq(total, 100);
}

[[cccc::test(exit_code = 255)]]
void test_prop_chain_in_loop_oob(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){10, 20, 30, 40};
    int *q = p;
    for (int i = 0; i < 4; i++) {
        int *r = q;
        (void)r;
        q++;
    }
    int *r = q; // one past the end after 4 plain q++'s
    int x = *r;
    (void)x;
}

// ---------------------------------------------------------------------
// #942 -- path-sensitive propagation via runtime snapshot validity. #919's
// rule was whole-function/straight-line: ONE non-checked-rooted store to a
// candidate, anywhere, disqualified it for the whole function -- even on a
// path where the candidate provably does hold a checked-rooted value.
// `int *q = malloc(...); if (c) q = p; q[i];` never propagated at all, even
// on the `c` branch. A conservative static join (kill the fact wherever
// it's not live on every incoming edge) doesn't fix this either -- it's
// still dead after the join. Instead, propagate_checked_bounds() now
// classifies each surviving candidate as FULL (every store checked-rooted
// -- byte-identical codegen to #919/#941, proven by the untouched tests
// above) or OPT (a mix): an OPT candidate's snapshot temps are refreshed at
// EVERY store, rooted or not (a non-rooted store writes an explicit
// [lo=-1, hi=0) sentinel instead of skipping the refresh), and at function
// entry, so the temps always reflect whichever store actually executed on
// this path. CHKRO (src/ops.c) then simply no-ops when it reads the
// sentinel. No CFG, no join, no fixpoint over a graph -- the runtime state
// naturally follows whichever path execution actually took.
// ---------------------------------------------------------------------

[[cccc::test]]
void test_opt_unrooted_path_not_checked(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    volatile int c = 0;
    void *buf = malloc(sizeof(int) * 16);
    int *q = buf;
    if (c)
        q = p;
    q[10] = 99; // c==0: q holds buf (unrooted) -- not checked, no trap
    AssertEq(q[10], 99);
    free(buf);
}

[[cccc::test]]
void test_opt_rooted_path_checked_in_bounds(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    volatile int c = 1;
    void *buf = malloc(sizeof(int) * 16);
    int *q = buf;
    if (c)
        q = p;
    AssertEq(q[2], 3); // c==1: q holds p (rooted) -- checked, in bounds
    free(buf);
}

[[cccc::test(exit_code = 255)]]
void test_opt_rooted_path_checked_oob(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    volatile int c = 1;
    void *buf = malloc(sizeof(int) * 16);
    int *q = buf;
    if (c)
        q = p;
    int x = q[10]; // c==1: q holds p -- OOB against p's count(4), must trap
    (void)x;
}

// The uninitialized-declaration shape of the same headline case -- #942
// extended candidate registration to cover `int *q;` (no initializer),
// which #919/#941 never registered as a candidate at all.
[[cccc::test]]
void test_opt_uninitialized_unrooted_path_not_checked(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    volatile int c = 0;
    void *buf = malloc(sizeof(int) * 16);
    int *q;
    if (c)
        q = p;
    else
        q = buf;
    q[10] = 99; // c==0: q holds buf -- not checked, no trap
    AssertEq(q[10], 99);
    free(buf);
}

[[cccc::test(exit_code = 255)]]
void test_opt_uninitialized_rooted_path_checked_oob(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    volatile int c = 1;
    void *buf = malloc(sizeof(int) * 16);
    int *q;
    if (c)
        q = p;
    else
        q = buf;
    int x = q[10]; // c==1: q holds p -- OOB against p's count(4), must trap
    (void)x;
}

// Flow-sensitivity/kill-set behavior falls out of the same runtime
// mechanism for free: `q[i]` is checked (its most recent store, `q = p`,
// was rooted), `q[j]` is not (its most recent store, `q = malloc(...)`,
// wasn't) -- no separate kill-set tracking needed, the snapshot temps
// simply hold whatever the last executed store put there.
[[cccc::test]]
void test_opt_flow_sensitive_kill_and_revive(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    void *buf;
    int *q = p;
    AssertEq(q[2], 3); // checked: q's most recent store is rooted (init)
    buf = malloc(sizeof(int) * 16);
    q = buf;
    q[10] = 7; // not checked: q's most recent store is unrooted
    AssertEq(q[10], 7);
    free(buf);
}

// #941-chain composition: `r`'s own single store IS checked-rooted (from
// `q`), but `q` itself is only OPT -- `r` must inherit OPT-ness
// transitively (Obj.checked_prop_optional propagated through
// checked_prop_scan_src_optional), or this would be exactly the false-trap
// hazard #942's design review flagged: a naively-FULL `r` would read `q`'s
// sentinel through plain CHKR and trap on this correct, unrooted-path code.
[[cccc::test]]
void test_opt_chain_through_optional_source_not_checked(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    volatile int c = 0;
    void *buf = malloc(sizeof(int) * 16);
    int *q = buf;
    if (c)
        q = p;
    int *r = q + 1; // r's own store is unconditionally rooted (from q)
    r[10] = 5;       // c==0: q (and therefore r) is unrooted -- no trap
    AssertEq(r[10], 5);
    free(buf);
}

[[cccc::test(exit_code = 255)]]
void test_opt_chain_through_optional_source_oob(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    volatile int c = 1;
    void *buf = malloc(sizeof(int) * 16);
    int *q = buf;
    if (c)
        q = p;
    int *r = q + 1;
    int x = r[10]; // c==1: r chains from p through q -- OOB, must trap
    (void)x;
}

// Three-deep chain through an OPT source -- OPT-ness must propagate past
// more than one hop.
[[cccc::test]]
void test_opt_chain_three_deep_through_optional_source_not_checked(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    volatile int c = 0;
    void *buf = malloc(sizeof(int) * 16);
    int *q = buf;
    if (c)
        q = p;
    int *r = q + 1;
    int *s = r + 1;
    s[10] = 5; // c==0: whole chain unrooted -- no trap
    AssertEq(s[10], 5);
    free(buf);
}

// OPT candidate whose only rooted store is reached through a loop back
// edge -- the sentinel must be live before the loop's first iteration
// (phase B' entry init) and the real snapshot must be live after the
// rooted store executes.
[[cccc::test]]
void test_opt_loop_back_edge_rooted_store(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    void *buf = malloc(sizeof(int) * 16);
    int *q = buf;
    int total = 0;
    for (int i = 0; i < 3; i++) {
        if (i == 1)
            q = p; // rooted only on this one iteration
        else if (i == 2)
            total += q[2]; // checked: q's most recent store (i==1) was rooted
    }
    AssertEq(total, 3);
    free(buf);
}

// `switch` fallthrough must not confuse the classifier or cause a false
// trap on the branch whose only reachable store is unrooted.
[[cccc::test]]
void test_opt_switch_fallthrough_not_checked(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    volatile int c = 0;
    void *buf = malloc(sizeof(int) * 16);
    int *q = buf;
    switch (c) {
    case 1:
        q = p;
        break;
    default:
        break;
    }
    q[10] = 42; // c==0 (default): q holds buf -- not checked, no trap
    AssertEq(q[10], 42);
    free(buf);
}

// `goto` past a rooted store must not cause a false trap on the path that
// skips it -- the entry sentinel (phase B') covers exactly this.
[[cccc::test]]
void test_opt_goto_skips_rooted_store_not_checked(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    void *buf = malloc(sizeof(int) * 16);
    int *q;
    goto skip;
    q = p;
skip:
    q = buf;
    q[10] = 7; // reached only via goto -- q holds buf, not checked, no trap
    AssertEq(q[10], 7);
    free(buf);
}

// #943: CHKNT propagation must also hold for an OPT candidate, not just a
// FULL one -- on the path that actually rooted `q` in `s`, the store into
// the terminator slot traps exactly like the FULL case above.
[[cccc::test(exit_code = 255)]]
void test_opt_chknt_propagates_on_rooted_path(void) {
    int n = 3;
    volatile int c = 1;
    void *buf = malloc(8);
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    char *q = buf;
    if (c)
        q = s;
    q[3] = 'x'; // c is true this run -- q holds s, traps via CHKNT
}

// #943: RMW through an OPT candidate on its rooted path must also trap --
// walk 2 (checked_prop_rewrite_scan()) has already wrapped this candidate's
// stores into ND_COMMA expressions by the time walk 3
// (checked_prop_attach_scan()) runs the mirror-stamping this depends on;
// this proves that rewrite doesn't disturb the checked_rmw_mirror back-link
// to_assign() set at parse time.
[[cccc::test(exit_code = 255)]]
void test_opt_chknt_rmw_propagates_on_rooted_path(void) {
    int n = 3;
    volatile int c = 1;
    void *buf = malloc(8);
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    char *q = buf;
    if (c)
        q = s;
    q[3] += 1; // c is true this run -- q holds s, RMW traps via CHKNT
}

// #943: the OPT candidate's UNROOTED path must not false-trap -- CHKRO's own
// sentinel handling already makes the range check a no-op there, and CHKNT
// must follow suit (compute_checked_bounds()/op_CHKNT_fn's `hi < elem_size`
// early-out covers the sentinel hi == 0 case).
[[cccc::test]]
void test_opt_chknt_unrooted_path_no_trap(void) {
    int n = 3;
    volatile int c = 0;
    void *buf = malloc(8);
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    char *q = buf;
    if (c)
        q = s;
    q[3] = 'x'; // c is false this run -- q holds buf, not checked, no trap
    AssertEq(q[3], 'x');
    free(buf);
}

