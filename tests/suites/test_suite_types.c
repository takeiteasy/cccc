// CCCC_FLAGS: --testing
// Consolidated suite: type sizes, char, casts, sizeof
// Source tests: test_char_as_int, test_int_size, test_long_size,
//               test_char_return, test_cast_expressions, test_sizeof,
//               test_serialize_typedef_roundtrip, test_serialize_types

// [from test_serialize_typedef_roundtrip] — file-scope type aliases for -M regression
typedef int SerTR_Int;
typedef SerTR_Int *SerTR_IntPtr;
typedef SerTR_Int SerTR_IntArray[3];
struct SerTR_Tagged { SerTR_Int value; };
typedef struct SerTR_Tagged SerTR_TaggedAlias;
typedef struct { SerTR_Int width; SerTR_Int height; } SerTR_AnonStruct;
typedef enum { SER_TR_RED = 1, SER_TR_GREEN = 2 } SerTR_EnumAlias;
typedef union { SerTR_Int i; char c; } SerTR_UnionAlias;

// [from test_serialize_types] — file-scope struct/union/enum for -M regression
typedef struct { int width; int height; } SerT_Size;
typedef enum { SER_T_RED = 1, SER_T_GREEN = 2 } SerT_Color;
union SerT_Value { int i; char c; };
struct SerT_Point { int x; int y; };
struct SerT_Box { struct SerT_Point origin; SerT_Size size; union SerT_Value value; SerT_Color color; };

#pragma cccc suite begin "types"

// test_int_size
[[cccc::test]]
void test_sizeof_int(void) {
    int x;
    AssertEq((int)sizeof(x), 4);
}

// test_long_size
[[cccc::test]]
void test_sizeof_long(void) {
    long x;
    AssertEq((int)sizeof(x), 8);
}

// test_sizeof
[[cccc::test]]
void test_sizeof_various(void) {
    AssertEq((int)sizeof(char),  1);
    AssertEq((int)sizeof(int),   4);
    AssertEq((int)sizeof(long),  8);
    AssertEq((int)sizeof(int*),  8);
    AssertEq((int)sizeof(char*), 8);
    int arr[5];
    AssertEq((int)sizeof(arr), 20);
    struct Point { int x; int y; };
    AssertEq((int)sizeof(struct Point), 8);
    int x;
    AssertEq((int)sizeof(x), 4);
    char c;
    AssertEq((int)sizeof(c), 1);
    AssertEq((int)sizeof(x + 1),   4);
    AssertEq((int)sizeof(arr[0]),   4);
}

// test_char_as_int: char truncation of 1000 → -24 or 232 (impl-defined sign)
[[cccc::test]]
void test_char_truncation(void) {
    int i = 1000;
    char c = (char)i;
    int c_as_int = c;
    Assert(c_as_int == 232 || c_as_int == -24);
}

// test_char_return: signed char -24 path
[[cccc::test]]
void test_char_return(void) {
    int i = 1000;
    char c = (char)i;
    int c_as_int = c;
    AssertEq(c_as_int, -24);
}

// test_cast_expressions: helper functions retained as statics
static void use_params(int a, int b) { (void)a; (void)b; }

static int cast_void_casts(void) {
    int x = 10, y = 20;
    (void)x; (void)(x + y); (void)(x * 2);
    return 42;
}
static int cast_numeric_casts(void) {
    int x = 10;
    double d = 3.14;
    (int)d; (double)x; (long)x; (char)x;
    int result = (int)d + x;
    result += (int)(d * 2);
    return result;    // 19
}
static int cast_pointer_casts(void) {
    int x = 42;
    int *p = &x;
    (void *)p; (char *)p; (long *)p;
    int value = *(int *)(void *)p;
    return value;     // 42
}
static int cast_with_side_effects(void) {
    int x = 5;
    (void)(x++);
    (void)(x += 10);
    return x;         // 16
}
static int identity_fn(int x) { return x; }
static int cast_in_call(void) {
    double d = 7.9;
    return identity_fn((int)d);  // 7
}

// [from test_serialize_typedef_roundtrip]
// Regression: -M typedef alias serialization round-trip; returns sum-25=42.
[[cccc::test(return = 42)]]
int test_serialize_typedef_roundtrip(void) {
    typedef struct { SerTR_Int x; SerTR_Int y; } LocalPoint;
    typedef SerTR_Int LocalScalar;
    SerTR_Int n = 10;
    SerTR_TaggedAlias tagged; tagged.value = 5;
    SerTR_AnonStruct anon; anon.width = 6; anon.height = 7;
    SerTR_EnumAlias color = SER_TR_GREEN;
    SerTR_UnionAlias uni; uni.i = 8;
    LocalPoint point; point.x = 9; point.y = 10;
    LocalScalar local = 4;
    SerTR_Int sum = 0;
    sum = sum + n;
    sum = sum + 1;
    sum = sum + 2;
    sum = sum + 3;
    sum = sum + tagged.value;
    sum = sum + anon.width;
    sum = sum + anon.height;
    sum = sum + color;
    sum = sum + uni.i;
    sum = sum + point.x;
    sum = sum + point.y;
    sum = sum + local;
    return sum - 25; // 67-25=42
}

// [from test_serialize_types]
// Regression: -M type definition serialization round-trip.
[[cccc::test(return = 42)]]
int test_serialize_types(void) {
    struct SerT_Box box;
    box.origin.x = 10;
    box.origin.y = 20;
    box.size.width = 5;
    box.size.height = 4;
    box.value.i = 1;
    box.color = SER_T_GREEN;
    return box.origin.x + box.origin.y + box.size.width + box.size.height +
           box.value.i + box.color; // 10+20+5+4+1+2=42
}

[[cccc::test]]
void test_cast_expressions(void) {
    use_params(1, 2);
    int result = 0;
    result += cast_void_casts();       // 42
    result += cast_numeric_casts();    // 19 → 61
    result += cast_pointer_casts();    // 42 → 103
    result += cast_with_side_effects(); // 16 → 119
    result += cast_in_call();          // 7  → 126
    AssertEq(result, 126);
}

#pragma cccc suite end
