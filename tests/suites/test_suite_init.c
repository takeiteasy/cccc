// CCCC_FLAGS: --testing
// Consolidated suite: initializers: designated, compound literals, compound assign
// Source tests: test_compound_assign_comprehensive, test_compound_literals, test_designated_array_simple, test_designated_nested, test_designated_struct_simple, test_designated_summary, test_indexed_store_call_index, test_init

// [from test_compound_assign_comprehensive]
// Comprehensive test for compound assignment operators
// Tests: Edge cases, arrays, pointers, struct members, globals, and complex expressions
// Returns: 42

int global = 100;

struct tc_compound_assign_comprehensive_Point {
    int x;
    int y;
};

// [from ticket #960]
// A designated initializer targeting a field of an *anonymous union*
// member (reached via a top-level designator, both in a compound literal
// and a plain brace initializer, and at both local and global scope) used
// to segfault the parser -- struct_designator() only special-cased
// anonymous *struct* members, so an anonymous union member fell through
// to a NULL `mem->name` deref. Anonymous struct members and named union
// members were unaffected; this suite covers all four shapes plus the
// still-working control cases so a regression in either is caught.
struct tc_anon_union_desig_S {
    union { int i; float f; };
    int tag;
};

struct tc_anon_union_desig_Named {
    union {
        int i;
        float f;
    } v;
    int tag;
};

struct tc_anon_union_desig_AnonStruct {
    struct { int i; };
    int tag;
};

union tc_anon_union_desig_NestedUnion {
    union { int i; float f; };
    long l;
};

// Global compound-literal-shaped initializer, exercises write_gvar_data's
// TY_UNION arm rather than create_lvar_init's.
struct tc_anon_union_desig_S tc_anon_union_desig_global = {.i = 7, .tag = 1};

// [from test_compound_literals]
// Test compound literals in CCCC
// Expected return: 42

// [from test_designated_array_simple]
/*
 * Simple test for array designated initializers: { [index] = value }
 * This tests if the parser recognizes the [index] = value syntax.
 */

// [from test_designated_nested]
/*
 * Test nested designated initializers
 */

struct tc_designated_nested_Point {
    int x;
    int y;
};

struct tc_designated_nested_Rect {
    struct tc_designated_nested_Point top_left;
    struct tc_designated_nested_Point bottom_right;
};

// [from test_designated_struct_simple]
/*
 * Simple test for struct designated initializers: { .member = value }
 * This tests if the parser recognizes the .member = value syntax.
 */

struct tc_designated_struct_simple_Point {
    int x;
    int y;
};

struct tc_designated_struct_simple_Rect {
    int width;
    int height;
    int depth;
};

// [from test_designated_summary]
/*
 * Test summary for designated initializers support in CCCC
 * 
 * FULLY WORKING:
 * - Array designated initializers: [index] = value
 * - Multiple array designators: [0] = 1, [2] = 3, [4] = 5
 * - Struct designated initializers: .member = value
 * - Out-of-order struct designators: .y = 30, .x = 40
 * - Single nested designator: .top_left.x = 10
 * - Multiple nested designators: .tl.x = 10, .tl.y = 20  ✅ FIXED!
 */

struct tc_designated_summary_Point {
    int x;
    int y;
};

struct tc_designated_summary_Rect {
    struct tc_designated_summary_Point tl;
    struct tc_designated_summary_Point br;
};

// [from test_indexed_store_call_index]
// Regression: indexed loads/stores at -O2+ go through the fused
// LDR_INDEX/STR_INDEX opcodes. Two distinct codegen bugs lived here (#581):
//
//   1. An *unsigned* post-increment index `buf[n++]` lowers to
//      `(unsigned)((n += 1) - 1)`, which evaluates to `1 + 0xFFFFFFFF ==
//      0x100000000` in a 64-bit register. match_indexed_addr strips the
//      widening cast that would have truncated it, so without an explicit
//      re-truncation the index carried a stray bit-32 and addressed ~4 GiB out
//      of bounds -> SIGSEGV.
//
//   2. When the index expression contains a call, the fused opcode held a base
//      address in a caller-saved temp across the call, which clobbered it.
//
// All paths must agree with the unfused -O0/-O1 result.

static unsigned ucount(void) { return 3; }

#pragma cccc suite begin "init"

// test_compound_assign_comprehensive
[[cccc::test(return = 42)]]
int test_compound_assign_comprehensive(void) {
    // Test 1: Compound assignment with arrays
    int arr[5];
    arr[0] = 10;
    arr[0] += 32;  // arr[0] = 42
    if (arr[0] != 42) return 1;
    
    // Test 2: Compound assignment with array indexing via variable
    int idx = 1;
    arr[idx] = 20;
    arr[idx] *= 2;  // arr[1] = 40
    arr[idx] += 2;  // arr[1] = 42
    if (arr[idx] != 42) return 2;
    
    // Test 3: Compound assignment with pointer dereference
    int val = 30;
    int *ptr = &val;
    *ptr += 12;  // *ptr = 42
    if (val != 42) return 3;
    if (*ptr != 42) return 4;
    
    // Test 4: Compound assignment with struct members
    struct tc_compound_assign_comprehensive_Point p;
    p.x = 40;
    p.x += 2;  // p.x = 42
    if (p.x != 42) return 5;
    
    // Test 5: Compound assignment with global variable
    global -= 58;  // global = 100 - 58 = 42
    if (global != 42) return 6;
    
    // Test 6: Compound assignment in loop
    int sum = 0;
    int i;
    for (i = 0; i < 10; i = i + 1) {
        sum += i;  // sum = 0+1+2+3+4+5+6+7+8+9 = 45
    }
    if (sum != 45) return 7;
    
    // Test 7: Multiple compound assignments in expression
    int a = 10;
    int b = 20;
    a += 5;  // a = 15
    b += 7;  // b = 27
    if (a + b != 42) return 8;
    
    // Test 8: Compound assignment with complex right-hand side
    int x = 10;
    int y = 5;
    int z = 3;
    x += y * z + 7;  // x = 10 + (5*3 + 7) = 10 + 22 = 32
    if (x != 32) return 9;
    
    // Test 9: All bitwise compound assignments
    int bits = 0;
    bits |= 2;   // bits = 2
    bits |= 8;   // bits = 10
    bits |= 32;  // bits = 42
    if (bits != 42) return 10;
    
    // Test 10: Shift operators with variables
    int shift = 21;
    int shift_amt = 1;
    shift <<= shift_amt;  // shift = 42
    if (shift != 42) return 11;
    
    // Test 11: Modulo with compound assignment
    int mod = 242;
    mod %= 200;  // mod = 42
    if (mod != 42) return 12;
    
    // Test 12: Division with compound assignment
    int div = 126;
    div /= 3;  // div = 42
    if (div != 42) return 13;
    
    // Test 13: Nested struct member compound assignment
    struct tc_compound_assign_comprehensive_Point points[2];
    points[0].x = 20;
    points[0].y = 22;
    points[0].x += points[0].y;  // points[0].x = 42
    if (points[0].x != 42) return 14;
    
    // Test 14: Compound assignment as condition
    int cond = 41;
    if ((cond += 1) != 42) return 15;
    
    // Test 15: XOR compound assignment
    int xor_val = 60;  // 0b111100
    xor_val ^= 22;     // 0b111100 ^ 0b010110 = 0b101010 = 42
    if (xor_val != 42) return 16;
    
    // Test 16: Chain multiple operations
    int chain = 5;
    chain += 5;   // 10
    chain *= 2;   // 20
    chain += 10;  // 30
    chain += 12;  // 42
    if (chain != 42) return 17;
    
    // Test 17: Compound assignment with subtraction (instead of negative)
    int neg = 50;
    neg -= 8;  // neg = 42
    if (neg != 42) return 18;
    
    // Test 18: Right shift with compound assignment
    int rshift = 168;
    rshift >>= 2;  // 168 >> 2 = 42
    if (rshift != 42) return 19;
    
    // All tests passed!
    return 42;
}

// test_compound_literals
[[cccc::test(return = 42)]]
int test_compound_literals(void) {
    // Test 1: Scalar compound literal
    int x = (int){42};
    if (x != 42) return 1;
    
    // Test 2: Scalar compound literal in expression
    int y = (int){30} + (int){12};
    if (y != 42) return 2;
    
    // Test 3: Multiple scalar compound literals
    int a = (int){10};
    int b = (int){20};
    int c = (int){12};
    if (a + b + c != 42) return 3;
    
    // Test 4: Compound literal assigned multiple times
    int val1 = (int){100};
    val1 = (int){42};
    if (val1 != 42) return 4;
    
    return 42;  // Success
}

// test_designated_array_simple
[[cccc::test(return = 42)]]
int test_designated_array_simple(void) {
    // Test 1: Single designated initializer
    int arr1[5] = {[2] = 42};
    
    if (arr1[0] != 0) return 1;   // Should be zero-initialized
    if (arr1[1] != 0) return 2;   // Should be zero-initialized
    if (arr1[2] != 42) return 3;  // Our designated value
    if (arr1[3] != 0) return 4;   // Should be zero-initialized
    if (arr1[4] != 0) return 5;   // Should be zero-initialized
    
    // Test 2: Multiple designated initializers
    int arr2[6] = {[0] = 10, [2] = 20, [5] = 30};
    
    if (arr2[0] != 10) return 6;
    if (arr2[1] != 0) return 7;
    if (arr2[2] != 20) return 8;
    if (arr2[3] != 0) return 9;
    if (arr2[4] != 0) return 10;
    if (arr2[5] != 30) return 11;
    
    // Test 3: Designated initializer at the end
    int arr3[3] = {[2] = 99};
    
    if (arr3[2] != 99) return 12;
    
    // Test 4: Designated initializer at the beginning
    int arr4[4] = {[0] = 7, [3] = 8};
    
    if (arr4[0] != 7) return 13;
    if (arr4[3] != 8) return 14;
    
    // Test 5: Use in expression
    int arr5[3] = {[1] = 15, [2] = 27};
    int sum = arr5[1] + arr5[2];  // 15 + 27 = 42
    
    if (sum != 42) return 15;
    
    return 42;
}

// test_designated_nested
[[cccc::test(return = 42)]]
int test_designated_nested(void) {
    // Test 1: Try the syntax that failed: .top_left.x = value
    struct tc_designated_nested_Rect r1 = {.top_left.x = 10};
    
    if (r1.top_left.x != 10) return 1;
    
    return 42;
}

// test_designated_struct_simple
[[cccc::test(return = 42)]]
int test_designated_struct_simple(void) {
    // Test 1: Basic struct designated initializer
    struct tc_designated_struct_simple_Point p1 = {.x = 10, .y = 20};
    
    if (p1.x != 10) return 1;
    if (p1.y != 20) return 2;
    
    // Test 2: Out-of-order designated initializers
    struct tc_designated_struct_simple_Point p2 = {.y = 30, .x = 40};
    
    if (p2.x != 40) return 3;
    if (p2.y != 30) return 4;
    
    // Test 3: Partial designated initialization (rest should be zero)
    struct tc_designated_struct_simple_Rect r1 = {.width = 100};
    
    if (r1.width != 100) return 5;
    if (r1.height != 0) return 6;  // Should be zero-initialized
    if (r1.depth != 0) return 7;   // Should be zero-initialized
    
    // Test 4: Only last field
    struct tc_designated_struct_simple_Rect r2 = {.depth = 50};
    
    if (r2.width != 0) return 8;   // Should be zero-initialized
    if (r2.height != 0) return 9;  // Should be zero-initialized
    if (r2.depth != 50) return 10;
    
    // Test 5: Mix of designated fields
    struct tc_designated_struct_simple_Rect r3 = {.height = 25, .depth = 75};
    
    if (r3.width != 0) return 11;
    if (r3.height != 25) return 12;
    if (r3.depth != 75) return 13;
    
    // Test 6: Use in expression
    struct tc_designated_struct_simple_Point p3 = {.x = 15, .y = 27};
    int sum = p3.x + p3.y;  // 15 + 27 = 42
    
    if (sum != 42) return 14;
    
    return 42;
}

// test_designated_summary
[[cccc::test(return = 42)]]
int test_designated_summary(void) {
    int result = 0;
    
    // ✅ Array designated initializers work
    int arr1[5] = {[2] = 10, [4] = 20};
    result += arr1[2];  // 10
    result += arr1[4];  // 20, total: 30
    
    // ✅ Struct designated initializers work
    struct tc_designated_summary_Point p1 = {.x = 5, .y = 7};
    result += p1.x + p1.y;  // 12, total: 42
    
    // ✅ Out-of-order works
    struct tc_designated_summary_Point p2 = {.y = 100, .x = 0};
    if (p2.x == 0 && p2.y == 100) {
        result += 10;  // total: 52
    }
    
    // ✅ Single nested designator works
    struct tc_designated_summary_Rect r1 = {.tl.x = 90};
    result += r1.tl.x;  // total: 142
    
    // ✅ Multiple nested designators NOW WORK!
    struct tc_designated_summary_Rect r2 = {.tl.x = 10, .tl.y = 20, .br.x = 30, .br.y = 40};
    if (r2.tl.x == 10 && r2.tl.y == 20 && r2.br.x == 30 && r2.br.y == 40) {
        result += 8;  // total: 150
    }

    if (result != 150) return 1;  // Assert result == 150
    return 42;
}

// test_indexed_store_call_index
[[cccc::test(return = 42)]]
int test_indexed_store_call_index(void) {
    char buf[64];
    for (int i = 0; i < 64; i++) buf[i] = 0;

    // (1) unsigned post-increment index (fused store)
    unsigned n = 0;
    for (int c = 'A'; c <= 'E'; c++)
        buf[n++] = (char)c;
    if (n != 5) return 1;
    if (buf[0] != 'A' || buf[1] != 'B' || buf[2] != 'C' ||
        buf[3] != 'D' || buf[4] != 'E')
        return 2;

    // unsigned post-increment index (fused load)
    unsigned r = 0;
    int acc = 0;
    while (r < 5)
        acc += buf[r++];
    if (acc != 'A' + 'B' + 'C' + 'D' + 'E') return 3;

    // (2) index expression containing a call
    buf[ucount()] = 'Z';
    if (buf[3] != 'Z') return 4;

    return 42;
}

// test_init
[[cccc::test(return = 42)]]
int test_init(void) {
    int a;
    a = 10;
    if (a != 10) return 1;  // Assert a == 10
    return 42;
}

// test_anon_union_designator_960
[[cccc::test(return = 42)]]
int test_anon_union_designator_960(void) {
    // Ticket #960's exact repro: compound literal, designator on an
    // anonymous union field.
    struct tc_anon_union_desig_S r = (struct tc_anon_union_desig_S){.i = 7, .tag = 1};
    if (r.i != 7 || r.tag != 1) return 1;

    // Same shape as a plain brace initializer (no compound literal) --
    // reaches the identical struct_designator() parser path.
    struct tc_anon_union_desig_S r2 = {.i = 7, .tag = 1};
    if (r2.i != 7 || r2.tag != 1) return 2;

    // Designator order reversed: exercises the struct_initializer2(...,
    // mem->next) continuation after the anonymous member.
    struct tc_anon_union_desig_S r3 = {.tag = 1, .i = 7};
    if (r3.i != 7 || r3.tag != 1) return 3;

    // Global with the same initializer shape (write_gvar_data path).
    if (tc_anon_union_desig_global.i != 7 || tc_anon_union_desig_global.tag != 1)
        return 4;

    // Control: named union member with a nested designator still works.
    struct tc_anon_union_desig_Named n = {.v = {.i = 7}, .tag = 1};
    if (n.v.i != 7 || n.tag != 1) return 5;

    // Control: anonymous struct member (the case that already worked)
    // still works after the fix.
    struct tc_anon_union_desig_AnonStruct a = {.i = 7, .tag = 1};
    if (a.i != 7 || a.tag != 1) return 6;

    // Anonymous union nested directly inside a union: exercises
    // union_initializer()'s own struct_designator() call.
    union tc_anon_union_desig_NestedUnion u = {.i = 9};
    if (u.i != 9) return 7;

    return 42;
}

#pragma cccc suite end
