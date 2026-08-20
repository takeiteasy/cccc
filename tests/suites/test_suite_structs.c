// CCCC_FLAGS: --testing
// Consolidated suite: structs, unions, bitfields, compound struct init
// Source tests: test_bitfields, test_compound_struct_minimal,
// test_compound_struct_test1, test_compound_struct_test2,
// test_compound_struct_test3, test_compound_structs, test_flexible_sizeof,
// test_offset, test_struct_byval, test_struct_if, test_struct_offset,
// test_struct_offset_b, test_struct_padding, test_struct_size,
// test_struct_values, test_union_byval, test_union_byval_simple,
// test_union_size

// [from test_bitfields]
// Comprehensive bitfield tests

struct bits1 {
    unsigned int a : 3;
    unsigned int b : 5;
    unsigned int c : 8;
};

struct bits2 {
    int x : 4; // Signed bitfield
    int y : 4;
};

struct tc_bitfields_mixed {
    unsigned int flags : 1;
    int          value;
    unsigned int more : 7;
};

// [from test_compound_struct_minimal]
// Minimal test for struct compound literal with address-of
// Expected return: 42

struct tc_compound_struct_minimal_Point {
    int x;
    int y;
};

// [from test_compound_struct_test1]
// Test struct compound literal - test 1 only
// Expected return: 42

struct tc_compound_struct_test1_Point {
    int x;
    int y;
};

// [from test_compound_struct_test2]
// Test 2 only: Direct member access on compound literal
// Expected return: 42

struct tc_compound_struct_test2_Point {
    int x;
    int y;
};

// [from test_compound_struct_test3]
// Test 3 only: Struct compound literal in expression
// Expected return: 42

struct tc_compound_struct_test3_Point {
    int x;
    int y;
};

// [from test_compound_structs]
// Test struct compound literals with pointer assignment
// Expected return: 42

struct tc_compound_structs_Point {
    int x;
    int y;
};

// [from test_flexible_sizeof]
// Debug sizeof for flexible array members

struct packet {
    int  size;
    char data[];
};

// [from test_offset]
// Test local variable offset handling

// [from test_struct_byval]
// Test struct by-value passing and returning
// Expected return: 42

struct tc_struct_byval_Point {
    int x;
    int y;
};

struct Rectangle {
    struct tc_struct_byval_Point top_left;
    struct tc_struct_byval_Point bottom_right;
};

// Return struct by value

static struct tc_struct_byval_Point make_point(int x, int y) {
    struct tc_struct_byval_Point p;
    p.x = x;
    p.y = y;
    return p;
}

// Pass struct by value and return by value

static struct tc_struct_byval_Point add_points(struct tc_struct_byval_Point a,
                                               struct tc_struct_byval_Point b) {
    struct tc_struct_byval_Point result;
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    return result;
}

// Pass struct by value, return scalar

static int point_sum(struct tc_struct_byval_Point p) {
    return p.x + p.y;
}

// Nested struct by value

static struct Rectangle make_rect(int x1, int y1, int x2, int y2) {
    struct Rectangle r;
    r.top_left.x     = x1;
    r.top_left.y     = y1;
    r.bottom_right.x = x2;
    r.bottom_right.y = y2;
    return r;
}

static int rect_area(struct Rectangle r) {
    int width  = r.bottom_right.x - r.top_left.x;
    int height = r.bottom_right.y - r.top_left.y;
    return width * height;
}

// [from test_struct_if]
// Combined test
// Expected return: 42

struct tc_struct_if_Point {
    int x;
    int y;
};

// [from test_struct_offset]
// Test if struct member offset issue exists
// Expected: 42 if correct, other value if bug

struct tc_struct_offset_Test {
    int a; // offset 0 (should be 0-7 in VM)
    int b; // offset 4 in chibicc, but should be 8-15 in VM
};

// [from test_struct_offset_b]
// Test if struct member offset issue exists - return b
// Expected: 200 if correct

struct tc_struct_offset_b_Test {
    int a; // offset 0
    int b; // offset 4 in chibicc, should be 8 in VM
};

// [from test_struct_padding]
// Test struct padding and alignment with different-sized types

struct tc_struct_padding_mixed {
    char c;  // offset 0, size 1
             // 1 byte padding
    short s; // offset 2, size 2
             // 0 bytes padding (already aligned)
    int  i;  // offset 4, size 4
    long l;  // offset 8, size 8
};

struct nested_padding {
    char a;                   // offset 0, size 1
                              // 3 bytes padding
    int  b;                   // offset 4, size 4
    char c;                   // offset 8, size 1
                              // 7 bytes padding
    long tc_struct_padding_d; // offset 16, size 8
};

// [from test_struct_size]
// Test sizeof struct
struct tc_struct_size_Point {
    int x;
    int y;
};

// [from test_struct_values]
// Test struct member values
// If a=100 and b=200, return 42
// Otherwise return error code

struct tc_struct_values_Test {
    int a;
    int b;
};

// [from test_union_byval]
// Test union by-value passing and returning
// Expected return: 42

union tc_union_byval_Data {
    int  i;
    char c;
    long l;
};

struct Pair {
    int first;
    int second;
};

union Mixed {
    int         i;
    struct Pair p;
};

// Return union by value

static union tc_union_byval_Data make_data_int(int value) {
    union tc_union_byval_Data tc_union_byval_d;
    tc_union_byval_d.i = value;
    return tc_union_byval_d;
}

// Pass union by value and return by value

static union tc_union_byval_Data
copy_data(union tc_union_byval_Data tc_union_byval_d) {
    union tc_union_byval_Data result;
    result.i = tc_union_byval_d.i;
    return result;
}

// Pass union by value, return scalar

static int get_int_from_data(union tc_union_byval_Data tc_union_byval_d) {
    return tc_union_byval_d.i;
}

// Union with struct member

static union Mixed make_mixed(int first, int second) {
    union Mixed m;
    m.p.first  = first;
    m.p.second = second;
    return m;
}

// [from test_union_byval_simple]
// Simple union by-value test
// Expected return: 42

union tc_union_byval_simple_Data {
    int  i;
    char c;
};

static union tc_union_byval_simple_Data make_data(int val) {
    union tc_union_byval_simple_Data d;
    d.i = val;
    return d;
}

static int get_value(union tc_union_byval_simple_Data d) {
    return d.i;
}

// [from test_union_size]
// Test union size calculation
union tc_union_size_Data {
    int  i;
    char c;
    long l;
};

// [from test_struct_fwd_const_member]
// Regression: const ptr to forward-declared struct must resolve after
// completion
struct tc_fwd_file {
    const struct tc_fwd_io_methods *pMethods; // incomplete at point of use
};
struct tc_fwd_io_methods {
    int (*xClose)(struct tc_fwd_file *);
    int (*xRead)(struct tc_fwd_file *, int);
};
static int tc_fwd_do_close(struct tc_fwd_file *f) {
    (void)f;
    return 40;
}
static int tc_fwd_do_read(struct tc_fwd_file *f, int n) {
    (void)f;
    return n;
}
static const struct tc_fwd_io_methods TC_FWD_METHODS = {tc_fwd_do_close,
                                                        tc_fwd_do_read};

// [from test_union_global]
union TcUnionGlobal {
    int  val;
    char ch;
} tc_union_global;

// [from test_union_pointer]
union TcUnionPtrData {
    int  i;
    char c;
};

// [from test_union_separate_assign]
union TcUnionSepData {
    int i;
};
static union TcUnionSepData tc_union_sep_make(void) {
    union TcUnionSepData d;
    d.i = 42;
    return d;
}

// [from test_union_bytes]
// Anonymous struct inside union for byte-level access
union TcUnionBytesAccess {
    int value;
    struct {
        char byte0;
        char byte1;
        char byte2;
        char byte3;
    } bytes;
};

// [from test_union_init]
union TcUnionInitData {
    int  i;
    long l;
    char c;
};
union TcUnionArrays {
    int  arr[3];
    char bytes[12];
};

// [from test_union_realworld]
// Struct with anonymous union member (type-tagged value)
#define TC_UNION_RW_INT  1
#define TC_UNION_RW_CHAR 2
struct TcUnionRealworldValue {
    int type;
    union {
        int   i;
        char  c;
        void *ptr;
    } data;
};

// [from test_union_advanced]
// Union with char-array member and nested union
union TcUnionAdvData {
    int  i;
    char bytes[8];
};
union TcUnionAdvNested {
    int  x;
    char a;
};

#pragma cccc suite begin "structs"

// test_bitfields
[[cccc::test(return = 42)]]
int test_bitfields(void) {
    // Test 1: Basic unsigned bitfield write/read
    struct bits1 x = {0};
    x.a            = 7;   // 3 bits: max value
    x.b            = 31;  // 5 bits: max value
    x.c            = 255; // 8 bits: max value

    if (x.a != 7)
        return 1;
    if (x.b != 31)
        return 2;
    if (x.c != 255)
        return 3;

    // Test 2: Overflow wrapping
    x.a = 8; // Should wrap to 0 (3 bits)
    if (x.a != 0)
        return 4;

    x.a = 15; // Should wrap to 7 (3 bits)
    if (x.a != 7)
        return 5;

    // Test 3: Signed bitfields
    struct bits2 y = {0};
    y.x            = 7;  // Max positive for 4-bit signed (-8 to 7)
    y.y            = -8; // Min negative for 4-bit signed

    if (y.x != 7)
        return 6;
    if (y.y != -8)
        return 7;

    // Test 4: Mixed struct with bitfields and regular members
    struct tc_bitfields_mixed m = {0};
    m.flags                     = 1;
    m.value                     = 12345;
    m.more                      = 127;

    if (m.flags != 1)
        return 8;
    if (m.value != 12345)
        return 9;
    if (m.more != 127)
        return 10;

    // Test 5: Bitfield assignment doesn't affect adjacent fields
    struct bits1 z = {0};
    z.b            = 31;
    if (z.a != 0 || z.c != 0)
        return 11;

    z.a = 5;
    if (z.b != 31) // b should still be 31
        return 12;

    return 42;
}

// test_compound_struct_minimal
[[cccc::test(return = 42)]]
int test_compound_struct_minimal(void) {
    // Test 1: Struct compound literal assigned to pointer
    struct tc_compound_struct_minimal_Point *p1 =
        &(struct tc_compound_struct_minimal_Point){30, 12};
    return 42;
}

// test_compound_struct_test1
[[cccc::test(return = 42)]]
int test_compound_struct_test1(void) {
    // Test 1: Struct compound literal assigned to pointer
    struct tc_compound_struct_test1_Point *p1 =
        &(struct tc_compound_struct_test1_Point){30, 12};
    int x_val = p1->x;
    int y_val = p1->y;
    int sum   = x_val + y_val;
    if (sum != 42)
        return 1;

    return 42;
}

// test_compound_struct_test2
[[cccc::test(return = 42)]]
int test_compound_struct_test2(void) {
    // Test 2: Direct member access on compound literal
    int val = ((struct tc_compound_struct_test2_Point){40, 2}).x +
              ((struct tc_compound_struct_test2_Point){40, 2}).y;
    if (val != 42)
        return 2;

    return 42;
}

// test_compound_struct_test3
[[cccc::test(return = 42)]]
int test_compound_struct_test3(void) {
    // Test 3: Struct compound literal in expression
    struct tc_compound_struct_test3_Point p2 =
        (struct tc_compound_struct_test3_Point){20, 22};
    if (p2.x + p2.y != 42)
        return 3;

    return 42;
}

// test_compound_structs
[[cccc::test(return = 42)]]
int test_compound_structs(void) {
    // Test 1: Struct compound literal assigned to pointer
    struct tc_compound_structs_Point *p1 =
        &(struct tc_compound_structs_Point){30, 12};
    if (p1->x + p1->y != 42)
        return 1;

    // Test 2: Direct member access on compound literal
    int val = ((struct tc_compound_structs_Point){40, 2}).x +
              ((struct tc_compound_structs_Point){40, 2}).y;
    if (val != 42)
        return 2;

    // Test 3: Struct compound literal in expression
    struct tc_compound_structs_Point p2 =
        (struct tc_compound_structs_Point){20, 22};
    if (p2.x + p2.y != 42)
        return 3;

    return 42;
}

// test_flexible_sizeof
[[cccc::test(return = 42)]]
int test_flexible_sizeof(void) {
    int s = sizeof(struct packet);
    // Return the actual sizeof value to see what it is
    if (s != 4)
        return 1; // Assert s == 4 (sizeof(int))
    return 42;
}

// test_offset
[[cccc::test(return = 42)]]
int test_offset(void) {
    int a = 10;
    int b = 20;
    int c = a + b;
    // c is at offset -3. When we read c:
    int eq = c == 30;
    if (eq != 1)
        return 1;
    return 42;
}

// test_struct_byval
[[cccc::test(return = 42)]]
int test_struct_byval(void) {
    // Test 1: Return struct by value
    struct tc_struct_byval_Point p1 = make_point(10, 20);
    if (p1.x != 10)
        return 1;
    if (p1.y != 20)
        return 2;

    // Test 2: Pass and return struct by value
    struct tc_struct_byval_Point p2 = make_point(5, 7);
    struct tc_struct_byval_Point p3 = add_points(p1, p2);
    if (p3.x != 15)
        return 3; // 10 + 5
    if (p3.y != 27)
        return 4; // 20 + 7

    // Test 3: Pass struct by value, return scalar
    int sum = point_sum(p3);
    if (sum != 42)
        return 5; // 15 + 27 = 42

    // Test 4: Nested structs
    struct Rectangle rect = make_rect(0, 0, 6, 7);
    int              area = rect_area(rect);
    if (area != 42)
        return 6; // 6 * 7 = 42

    // Test 5: Chain function calls
    struct tc_struct_byval_Point p4 =
        add_points(make_point(1, 2), make_point(3, 4));
    if (p4.x != 4)
        return 7;
    if (p4.y != 6)
        return 8;

    return 42;
}

// test_struct_if
[[cccc::test(return = 42)]]
int test_struct_if(void) {
    struct tc_struct_if_Point p;
    p.x        = 10;
    p.y        = 32;

    int result = p.x + p.y;

    if (result != 42) {
        return 1;
    }

    return 42;
}

// test_struct_offset
[[cccc::test(return = 42)]]
int test_struct_offset(void) {
    struct tc_struct_offset_Test tc_struct_offset_t;
    tc_struct_offset_t.a = 100;
    tc_struct_offset_t.b = 200;

    // If offsets overlap, b will overwrite part of a
    // Then a will have a corrupted value

    int a_val = tc_struct_offset_t.a;
    int b_val = tc_struct_offset_t.b;

    // Check if a is still 100
    if (a_val != 100)
        return 1;
    return 42;
}

// test_struct_offset_b
[[cccc::test(return = 42)]]
int test_struct_offset_b(void) {
    struct tc_struct_offset_b_Test tc_struct_offset_b_t;
    tc_struct_offset_b_t.a = 100;
    tc_struct_offset_b_t.b = 200;

    int b_val              = tc_struct_offset_b_t.b;
    if (b_val != 200)
        return 1;
    return 42;
}

// test_struct_padding
[[cccc::test(return = 42)]]
int test_struct_padding(void) {
    // Test struct tc_struct_padding_mixed layout
    // With proper padding: char(1) + pad(1) + short(2) + int(4) + long(8) = 16
    if (sizeof(struct tc_struct_padding_mixed) != 16)
        return 1;

    // Test nested_padding layout
    // With proper padding: a(1) + pad(3) + b(4) + c(1) + pad(7) +
    // tc_struct_padding_d(8) = 24
    if (sizeof(struct nested_padding) != 24)
        return 2;

    // Test that we can write and read values correctly with mixed sizes
    struct tc_struct_padding_mixed m;
    m.c = 10;
    m.s = 20;
    m.i = 30;
    m.l = 40;

    if (m.c != 10)
        return 3;
    if (m.s != 20)
        return 4;
    if (m.i != 30)
        return 5;
    if (m.l != 40)
        return 6;

    // Test nested struct
    struct nested_padding n;
    n.a                   = 100;
    n.b                   = 200;
    n.c                   = 50;
    n.tc_struct_padding_d = 300;

    if (n.a != 100)
        return 7;
    if (n.b != 200)
        return 8;
    if (n.c != 50)
        return 9;
    if (n.tc_struct_padding_d != 300)
        return 10;

    // Test struct with just different sizes to ensure no 8-byte assumption
    struct size_test {
        char  a;
        char  b;
        short c;
        int   tc_struct_padding_d;
    };

    // Should be: char(1) + char(1) + short(2) + int(4) = 8 (no padding needed
    // after alignment)
    if (sizeof(struct size_test) != 8)
        return 11;

    return 42; // All padding and alignment tests passed
}

// test_struct_size
[[cccc::test(return = 42)]]
int test_struct_size(void) {
    // In VM, int is 4 bytes, so struct tc_struct_size_Point should be 8 bytes
    int size = sizeof(struct tc_struct_size_Point);
    if (size != 8)
        return 1;
    return 42; // Success!
}

// test_struct_values
[[cccc::test(return = 42)]]
int test_struct_values(void) {
    struct tc_struct_values_Test tc_struct_values_t;
    tc_struct_values_t.a = 100;
    tc_struct_values_t.b = 200;

    int a_val            = tc_struct_values_t.a;
    int b_val            = tc_struct_values_t.b;

    if (a_val != 100)
        return 1; // a is wrong
    if (b_val != 200)
        return 2; // b is wrong

    return 42;    // Both correct
}

// test_union_byval
[[cccc::test(return = 42)]]
int test_union_byval(void) {
    // Test 1: Return union by value
    union tc_union_byval_Data d1 = make_data_int(42);
    if (d1.i != 42)
        return 1;

    // Test 2: Pass and return union by value
    union tc_union_byval_Data d2 = copy_data(d1);
    if (d2.i != 42)
        return 2;

    // Test 3: Pass union by value, return scalar
    int val = get_int_from_data(d2);
    if (val != 42)
        return 3;

    // Test 4: Union with struct member
    union Mixed mixed = make_mixed(10, 32);
    if (mixed.p.first != 10)
        return 4;
    if (mixed.p.second != 32)
        return 5;

    // Test 5: Modify union member
    union tc_union_byval_Data d3;
    d3.i = 100;
    d3   = make_data_int(42);
    if (d3.i != 42)
        return 6;

    return 42;
}

// test_union_byval_simple
[[cccc::test(return = 42)]]
int test_union_byval_simple(void) {
    union tc_union_byval_simple_Data d1 = make_data(42);
    if (d1.i != 42)
        return 1;

    int val = get_value(d1);
    if (val != 42)
        return 2;

    return 42;
}

// test_union_size
[[cccc::test(return = 42)]]
int test_union_size(void) {
    int size = sizeof(union tc_union_size_Data); // Should return 8 (size of
                                                 // long, which is 8 in VM)
    if (size != 8)
        return 1; // Assert sizeof(union tc_union_size_Data) == 8
    return 42;
}

// test_struct_fwd_const_member: const ptr to forward-declared struct regression
[[cccc::test(return = 42)]]
int test_struct_fwd_const_member(void) {
    struct tc_fwd_file f;
    f.pMethods  = &TC_FWD_METHODS;
    int r       = f.pMethods->xClose(&f);   // 40
    r          += f.pMethods->xRead(&f, 2); // +2
    return r;                               // 42
}

// test_union_global: file-scope union declaration and access
[[cccc::test(return = 42)]]
int test_union_global(void) {
    tc_union_global.val = 42;
    return tc_union_global.val;
}

// test_union_pointer: union pointer arrow access
[[cccc::test(return = 42)]]
int test_union_pointer(void) {
    union TcUnionPtrData d;
    d.i                       = 100;
    union TcUnionPtrData *ptr = &d;
    ptr->i                    = 42;
    return ptr->i;
}

// test_union_separate_assign: union return assigned separately (not at init)
[[cccc::test(return = 42)]]
int test_union_separate_assign(void) {
    union TcUnionSepData result;
    result = tc_union_sep_make();
    return result.i;
}

// test_union_bytes: anonymous struct inside union for byte-level access
[[cccc::test(return = 42)]]
int test_union_bytes(void) {
    union TcUnionBytesAccess u;
    u.value = 0x2A1E0F05; // bytes: 05 0F 1E 2A (little-endian)
    if (u.bytes.byte3 != 0x2A)
        return 1;         // 0x2A == 42
    return 42;
}

// test_union_init: union with array member and long member
[[cccc::test(return = 42)]]
int test_union_init(void) {
    int                   score = 0;

    union TcUnionInitData d1;
    d1.i = 100;
    d1.c = 42;
    if (d1.c == 42)
        score += 10;

    union TcUnionArrays a;
    a.arr[0] = 10;
    a.arr[1] = 20;
    a.arr[2] = 30;
    if (a.arr[0] == 10 && a.arr[1] == 20 && a.arr[2] == 30)
        score += 10;

    union TcUnionInitData d2;
    d2.c = 42;
    if (d2.c == 42)
        score += 10;

    union TcUnionInitData d3;
    d3.l = 12;
    if (d3.l == 12)
        score += 12;

    return score; // 42
}

// test_union_realworld: struct with anonymous union member (type-tagged value)
[[cccc::test(return = 42)]]
int test_union_realworld(void) {
    int                          score = 0;

    struct TcUnionRealworldValue v1;
    v1.type   = TC_UNION_RW_INT;
    v1.data.i = 100;
    if (v1.type == TC_UNION_RW_INT && v1.data.i == 100)
        score += 10;

    struct TcUnionRealworldValue v2;
    v2.type   = TC_UNION_RW_CHAR;
    v2.data.c = 'A';
    if (v2.type == TC_UNION_RW_CHAR && v2.data.c == 'A')
        score += 10;

    struct TcUnionRealworldValue v3;
    v3.type   = TC_UNION_RW_INT;
    v3.data.i = 200;
    v3.type   = TC_UNION_RW_CHAR;
    v3.data.c = 42;
    if (v3.type == TC_UNION_RW_CHAR && v3.data.c == 42)
        score += 10;

    struct TcUnionRealworldValue arr[3];
    arr[0].type   = TC_UNION_RW_INT;
    arr[0].data.i = 1;
    arr[1].type   = TC_UNION_RW_INT;
    arr[1].data.i = 2;
    arr[2].type   = TC_UNION_RW_INT;
    arr[2].data.i = 3;
    int sum       = arr[0].data.i + arr[1].data.i + arr[2].data.i;
    if (sum == 6)
        score += 12;

    return score; // 42
}

// test_union_advanced: char-array union member, nested union, pointer access
[[cccc::test(return = 42)]]
int test_union_advanced(void) {
    // char array member
    union TcUnionAdvData d;
    d.i         = 0x04030201;
    int byte_ok = (d.bytes[0] == 0x01) ? 10 : 0;

    // nested union (trivial; verifies union of different sizes)
    union TcUnionAdvNested n;
    n.a           = 10;
    int nested_ok = (n.a == 10) ? 10 : 0;

    // pointer arrow access
    union TcUnionAdvData *ptr = &d;
    ptr->i                    = 200;
    int ptr_ok                = (ptr->i == 200) ? 10 : 0;

    return byte_ok + nested_ok + ptr_ok + 12; // 10+10+10+12 = 42
}

#pragma cccc suite end
