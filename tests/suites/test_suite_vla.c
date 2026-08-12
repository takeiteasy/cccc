// CCCC_FLAGS: --testing
// Consolidated suite: variable-length arrays
// Source tests: test_vla_basic, test_vla_cleanup, test_vla_comprehensive, test_vla_enhancements, test_vla_expr, test_vla_nested, test_vla_param, test_vla_ultra_minimal,
//   test_vla_simple, test_vla_minimal, test_vla_init, test_vla_cleanup_simple, test_vla_with_constructor

// [from test_vla_basic]
// Test: VLA basic allocation
// Expected return: 42

// [from test_vla_cleanup]
// Test VLA memory cleanup on function exit
// Expected return: 42
// This test verifies that VLAs are properly freed when functions return

static int test_vla_in_function() {
    int n = 10;
    int arr[n];
    
    // Use the array
    arr[0] = 42;
    int result = arr[0];
    
    // VLA should be freed automatically when function returns
    return result;
}

static int test_multiple_vlas() {
    int n1 = 5;
    int n2 = 10;
    
    int arr1[n1];
    int arr2[n2];
    
    arr1[0] = 20;
    arr2[0] = 22;
    
    int result = arr1[0] + arr2[0];  // 20 + 22 = 42
    
    // Both VLAs should be freed automatically
    return result;
}

static int test_vla_in_nested_scopes() {
    int result = 0;
    
    {
        int n = 5;
        int arr[n];
        arr[0] = 10;
        result += arr[0];
    }
    
    {
        int n = 8;
        int arr[n];
        arr[0] = 32;
        result += arr[0];
    }
    
    return result;  // 10 + 32 = 42
}

// [from test_vla_comprehensive]
// Test: Comprehensive VLA test with multiple scenarios
// Expected return: 100

static int sum_array(int *arr, int size) {
    int sum = 0;
    for (int i = 0; i < size; i = i + 1) {
        sum = sum + arr[i];
    }
    return sum;
}

// [from test_vla_enhancements]
// Comprehensive test of VLA enhancements
// Expected return: 42

static int _vla_enhancements_test_vla_cleanup() {
    // Test automatic cleanup on return
    int n = 10;
    int arr[n];
    arr[0] = 42;
    return arr[0];  // VLA cleaned up automatically
}

static int test_multiple_returns() {
    int n = 5;
    int arr[n];
    
    arr[0] = 10;
    
    if (arr[0] == 10) {
        return 42;  // VLA cleaned up here
    }
    
    return 0;  // VLA would be cleaned up here too
}

// [from test_vla_expr]
// Test: VLA with expression for size
// Expected return: 84

// [from test_vla_nested]
// Test: VLA in nested scope
// Expected return: 30

// [from test_vla_param]
// Test: VLA array parameters decay to pointer (C99 §6.7.6.3p7)
// Ticket: #413 — void f(int n, int a[n]) previously errored "undefined variable 'n'"
// Expected return: 42

static int sum(int n, int a[n]) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s;
}

// Forward declaration exercises the is_function lookahead path
static int dot(int n, int a[n], int b[n]);

static int dot(int n, int a[n], int b[n]) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}

// Qualifiers on bracketed dimension transfer to the decayed pointer

static int sum_const(int n, const int a[n]) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s;
}

// [from test_vla_ultra_minimal]
// Test: Ultra minimal - just create VLA, don't use it
// Expected return: 1

#pragma cccc suite begin "vla"

// test_vla_basic
[[cccc::test(return = 42)]]
int test_vla_basic(void) {
    int n = 3;
    int arr[n];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 12;
    if (arr[0] + arr[1] + arr[2] != 42) return 1;
    return 42;
}

// test_vla_cleanup
[[cccc::test(return = 42)]]
int test_vla_cleanup(void) {
    int r1 = test_vla_in_function();
    if (r1 != 42) return 1;
    
    int r2 = test_multiple_vlas();
    if (r2 != 42) return 2;
    
    int r3 = test_vla_in_nested_scopes();
    if (r3 != 42) return 3;
    
    return 42;  // Success
}

// test_vla_comprehensive
[[cccc::test(return = 42)]]
int test_vla_comprehensive(void) {
    int n = 5;
    int arr[n];
    
    // Initialize array
    for (int i = 0; i < n; i = i + 1) {
        arr[i] = (i + 1) * 10;  // 10, 20, 30, 40, 50
    }
    
    // Test reading values
    int test1 = arr[0];  // 10
    int test2 = arr[4];  // 50
    
    // Test pointer arithmetic
    int *ptr = arr;
    int test3 = ptr[2];  // 30
    
    // Test passing VLA to function
    int total = sum_array(arr, n);  // 10+20+30+40+50 = 150
    
    // Verify results
    if (test1 != 10) return 1;
    if (test2 != 50) return 2;
    if (test3 != 30) return 3;
    if (total != 150) return 4;

    return 42;  // Success
}

// test_vla_enhancements
[[cccc::test(return = 42)]]
int test_vla_enhancements(void) {
    int r1 = _vla_enhancements_test_vla_cleanup();
    if (r1 != 42) return 1;
    
    int r2 = test_multiple_returns();
    if (r2 != 42) return 2;
    
    return 42;  // Success
}

// test_vla_expr
[[cccc::test(return = 42)]]
int test_vla_expr(void) {
    int x = 3;
    int y = 4;
    int arr[x + y];  // 7 elements
    
    for (int i = 0; i < 7; i = i + 1) {
        arr[i] = i * 10;
    }
    
    int sum = 0;
    for (int i = 0; i < 7; i = i + 1) {
        sum = sum + arr[i];
    }

    // sum = 0+10+20+30+40+50+60 = 210
    int result = sum / 2 - 21;  // 210/2 - 21 = 105 - 21 = 84
    if (result != 84) return 1;  // Assert result == 84
    return 42;
}

// test_vla_nested
[[cccc::test(return = 42)]]
int test_vla_nested(void) {
    int result = 0;
    
    {
        int n = 3;
        int arr1[n];
        arr1[0] = 10;
        arr1[1] = 20;
        arr1[2] = 30;
        result = arr1[2];
    }
    
    {
        int m = 2;
        int arr2[m];
        arr2[0] = 100;
        arr2[1] = 200;
        // result should still be 30 from above
    }

    if (result != 30) return 1;  // Assert result == 30
    return 42;
}

// test_vla_param
[[cccc::test(return = 42)]]
int test_vla_param(void) {
    int a[4] = {1, 2, 3, 4};
    int b[4] = {4, 3, 2, 1};

    if (sum(4, a) != 10) return 1;
    if (dot(4, a, b) != 20) return 2;
    if (sum_const(4, a) != 10) return 3;

    return 42;
}

// test_vla_ultra_minimal
[[cccc::test(return = 42)]]
int test_vla_ultra_minimal(void) {
    int n = 1;
    int arr[n];
    if (n != 1) return 0;  // Assert n == 1
    return 42;
}

// [from test_vla_simple]
[[cccc::test(return = 42)]]
int test_vla_simple(void) {
    int n = 5;
    int arr[n];
    arr[0] = 10; arr[1] = 20; arr[2] = 30; arr[3] = 40; arr[4] = 50;
    int sum = arr[0] + arr[1] + arr[2] + arr[3] + arr[4];
    return sum - 108; // 150 - 108 = 42
}

// [from test_vla_minimal]
[[cccc::test(return = 42)]]
int test_vla_minimal(void) {
    int n = 1;
    int arr[n];
    arr[0] = 42;
    return arr[0];
}

// [from test_vla_init]
// VLA with brace initializer.
[[cccc::test(return = 42)]]
int test_vla_init(void) {
    int n = 5;
    int arr[n] = {10, 20, 30, 40, 50};
    int sum = arr[0] + arr[1] + arr[2] + arr[3] + arr[4];
    return sum - 108; // 150 - 108 = 42
}

// [from test_vla_cleanup_simple]
[[cccc::test(return = 42)]]
int test_vla_cleanup_simple(void) {
    int n = 5;
    int arr[n];
    arr[0] = 42;
    return arr[0];
}

// [from test_vla_with_constructor]
// Regression #588: VLA + __attribute__((constructor)) must not SIGSEGV.
[[cccc::test(return = 42)]]
int test_vla_with_constructor(void) {
    __attribute__((constructor)) static void init_thing588(void) {}
    static int vla_sink(char *p, int n) { p[0] = (char)n; return p[0]; }
    int n = 8;
    char buf[n + 4];
    if ((int)sizeof(buf) != 12) return 1;
    return vla_sink(buf, 42);
}

// Regression #971: subscripting a multi-dimensional VLA used to SIGSEGV.
// `v[i]` (a row of a 2-D VLA) is itself VLA-typed -- the inner ND_DEREF must
// leave its computed row *address* alone rather than loading through it, or
// the outer subscript dereferences garbage. Both dimensions variable.
[[cccc::test(return = 42)]]
int test_vla_2d_subscript(void) {
    int n = 2, m = 3;
    int v[n][m];
    v[0][0] = 1; v[0][1] = 2; v[0][2] = 3;
    v[1][0] = 4; v[1][1] = 5; v[1][2] = 36;
    return v[0][0] + v[0][1] + v[0][2] + v[1][0] + v[1][1] + v[1][2] - 9;
}

// #971: only the outer dimension is variable -- the element type is a plain
// TY_ARRAY (already worked before the fix), kept as a control alongside the
// other shapes below.
[[cccc::test(return = 42)]]
int test_vla_2d_variable_outer_dim(void) {
    int n = 2;
    int v[n][3];
    v[1][2] = 42;
    return v[1][2];
}

// #971: only the inner dimension is variable -- a constant outer bound still
// makes the whole type a TY_VLA chain (array_dimensions' `ty->kind == TY_VLA`
// propagation), so this shape crashed before the fix too.
[[cccc::test(return = 42)]]
int test_vla_2d_variable_inner_dim(void) {
    int n = 3;
    int v[2][n];
    v[1][2] = 42;
    return v[1][2];
}

// #971: a VLA row decays to a pointer just like an array row does -- reading
// back through the decayed pointer must see the same storage.
[[cccc::test(return = 42)]]
int test_vla_2d_row_decay(void) {
    int n = 2, m = 3;
    int v[n][m];
    int *row = v[1];
    row[2] = 42;
    return v[1][2];
}

// #971: three dimensions, all variable -- exercises the middle dimension's
// vla_size (m*k, not just k) and two levels of address-only ND_DEREF.
[[cccc::test(return = 42)]]
int test_vla_3d_subscript(void) {
    int n = 2, m = 3, k = 4;
    int v[n][m][k];
    v[1][2][3] = 42;
    return v[1][2][3];
}

// #973: `&v` on a VLA local must yield the array's data address, not the
// frame slot that holds the alloca'd pointer -- `(void*)&v` and `(void*)v`
// (the array's own decay-to-pointer value) must be the same address.
[[cccc::test(return = 42)]]
int test_vla_addr_equals_decay(void) {
    int n = 3;
    int v[n];
    return (void *)&v == (void *)v ? 42 : 1;
}

// #973: `int (*p)[n] = &v;` must let `(*p)[i]` read/write the same storage
// as `v[i]` -- before the fix, `p` pointed at the frame slot instead of the
// VLA's data, so `(*p)[i]` read garbage.
[[cccc::test(return = 42)]]
int test_vla_addr_row_pointer_round_trip(void) {
    int n = 3;
    int v[n];
    v[0] = 7;
    v[1] = 8;
    v[2] = 42;
    int (*p)[n] = &v;
    (*p)[0] += 1;
    return (*p)[2] - (v[0] - 8);
}

// #973: `&v` has type `int (*)[n]` (pointer-to-VLA-row), which does NOT
// decay to `int *` the way a fixed-size array's `&a` does -- `&v + 1` must
// stride a whole row (n * sizeof(int)), not one element.
[[cccc::test(return = 42)]]
int test_vla_addr_stride_is_whole_row(void) {
    int n = 3;
    int v[n];
    long stride = (long)((char *)(&v + 1) - (char *)&v);
    return stride == (long)(n * sizeof(int)) ? 42 : 1;
}

// #973: `sizeof(*p)` for `int (*p)[n] = &v` must be the VLA's runtime row
// size, confirming &v's pointee type is the VLA itself, not its element.
[[cccc::test(return = 42)]]
int test_vla_addr_sizeof_deref(void) {
    int n = 3;
    int v[n];
    int (*p)[n] = &v;
    return sizeof(*p) == n * sizeof(int) ? 42 : 1;
}

// #973: the same &v value/type rules apply to a 2-D VLA -- &v's row pointer
// still reaches the same storage as the array itself.
[[cccc::test(return = 42)]]
int test_vla_2d_addr_equals_decay(void) {
    int n = 2, m = 3;
    int v[n][m];
    return (void *)&v == (void *)v ? 42 : 1;
}

// #973 control: `&v[1]` (address of an inner VLA row reached by pointer
// arithmetic, not the VLA variable itself) was already address-based and
// correct before this fix -- must keep working unchanged.
[[cccc::test(return = 42)]]
int test_vla_2d_addr_of_row(void) {
    int n = 2, m = 3;
    int v[n][m];
    int (*p)[m] = &v[1];
    p[0][0] = 42;
    return v[1][0];
}

// #976: `&v[1] - &v[0]` on a 2-D VLA must divide the byte difference by the
// row's runtime size (vla_size), not TY_VLA's placeholder pointer-sized
// `size` (8) -- and the pre-existing "VLA - num" arm must not intercept a
// pointer rhs before this ptr-ptr arm ever runs (it used to, unconditionally,
// whenever lhs was VLA-row-pointer-typed).
[[cccc::test(return = 42)]]
int test_vla_2d_row_ptr_sub(void) {
    int n = 2, m = 3;
    int v[n][m];
    long d1 = &v[1] - &v[0];
    return d1 == 1 ? 42 : 1;
}

// #976: the negative direction specifically discriminates the ty_ulong
// division trap -- vla_size is an unsigned long Obj, so dividing a signed
// byte difference by it without a cast to a signed type promotes the whole
// division to unsigned, turning -1 into a huge positive garbage value. A
// test that only checks the positive direction passes with that bug intact.
[[cccc::test(return = 42)]]
int test_vla_2d_row_ptr_sub_negative(void) {
    int n = 2, m = 3;
    int v[n][m];
    long d0 = &v[0] - &v[1];
    return d0 == -1 ? 42 : 1;
}

// #976 control: plain "VLA - num" pointer arithmetic (not ptr-ptr) must
// still work after guarding that arm against a pointer rhs.
[[cccc::test(return = 42)]]
int test_vla_row_ptr_minus_num(void) {
    int n = 2, m = 3;
    int v[n][m];
    int (*p)[m] = &v[1];
    int (*p0)[m] = p - 1;
    p0[0][0] = 42;
    return v[0][0];
}

// #977: a multi-dimensional VLA brace initializer used to be silently
// dropped -- create_lvar_init had no TY_VLA case, so a nested row's brace
// group (whose own type is TY_VLA, not a scalar/aggregate create_lvar_init
// already handled) fell through to the generic "no top-level expr" check
// and returned a no-op. Every element must now read back correctly.
[[cccc::test(return = 42)]]
int test_vla_2d_brace_init(void) {
    int n = 2, m = 2;
    int v[n][m] = {{1, 2}, {3, 4}};
    return v[0][0] + v[0][1] + v[1][0] * 10 + v[1][1] * 10 == 3 + 70 ? 42 : 1;
}

// #977: a ragged/short row (fewer initializers than the row width) must
// zero-fill the remainder, same as a fixed-size array's partial row.
[[cccc::test(return = 42)]]
int test_vla_2d_brace_init_ragged(void) {
    int n = 2, m = 2;
    int v[n][m] = {{1, 2}, {3}};
    return v[0][0] == 1 && v[0][1] == 2 && v[1][0] == 3 && v[1][1] == 0
               ? 42
               : 1;
}

// #977: a short outer initializer (fewer rows than the array's outer
// dimension) must zero-fill the missing rows entirely.
[[cccc::test(return = 42)]]
int test_vla_2d_brace_init_short(void) {
    int n = 2, m = 2;
    int v[n][m] = {{1, 2}};
    return v[0][0] == 1 && v[0][1] == 2 && v[1][0] == 0 && v[1][1] == 0
               ? 42
               : 1;
}

#pragma cccc suite end
