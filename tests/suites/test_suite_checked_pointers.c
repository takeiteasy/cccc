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

// byte_count()/bounds() forms on ntarray get no terminator-slot widening
// (#923's documented follow-up gap) -- a write at the declared edge is an
// ordinary CHKR OOB trap, not a CHKNT terminator trap.
[[cccc::test(exit_code = 255)]]
void test_ntarray_byte_count_no_widening_traps(void) {
    int nbytes = 3;
    char * [[cccc::ntarray, cccc::byte_count(nbytes)]] s =
        (char[4]){'a', 'b', 'c', 0};
    s[nbytes] = 0; // no widening for byte_count -- plain OOB
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

// #923's CHKNT does not propagate (documented gap) -- a non-null write to
// the widened terminator slot through a propagated ntarray pointer does not
// trap, unlike the direct-access case (test_ntarray_terminator_nonnull_traps
// above).
[[cccc::test]]
void test_prop_chknt_does_not_propagate(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    char *q = s;
    q[3] = 'x'; // would trap via CHKNT through `s` directly; not through `q`
    AssertEq(q[3], 'x');
}
