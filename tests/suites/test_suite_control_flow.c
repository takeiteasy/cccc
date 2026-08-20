// CCCC_FLAGS: --testing
// Consolidated suite: control flow: switch, goto, labels-as-values, edge cases
// Source tests: test_control_flow, test_edge_computed_goto_comma,
// test_edge_coroutine_switch, test_edge_duffs_device, test_edge_map_macro,
// test_edge_narg_macro, test_edge_try_catch, test_edge_url_as_label_comment,
// test_goto, test_label_value_static, test_labels_as_values,
// test_logicop_void_context, test_stmt_expr, test_switch,
// test_switch_codegen_optimized, test_switch_debug, test_switch_dense_100,
// test_switch_edge, test_switch_minimal, test_switch_nobreak

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

// [from test_edge_computed_goto_comma]
// Tests: computed goto whose target is a comma expression involving function
// calls — goto *expr1, expr2, target; where expr1/expr2 are calls (side
// effects) and the final element is the actual jump destination.
// The comma operator must be parsed as part of the goto expression, not as
// a separate statement. Both puts calls must execute before the jump.
// Previously caused a stack overflow because the goto target evaluated to 0,
// causing JMPI to jump to PC=0 (entry point), re-entering main infinitely.

// [from test_edge_coroutine_switch]
#define crBegin                                                                \
    static int state = 0;                                                      \
    switch (state) {                                                           \
        case 0:
#define crReturn(x)                                                            \
    do {                                                                       \
        state = __LINE__;                                                      \
        return (x);                                                            \
        case __LINE__:;                                                        \
    } while (0)
#define crFinish }

static int counter(void) {
    static int i;
    crBegin;
    for (i = 0; i < 5; i++)
        crReturn(i);
    crFinish;
    return -1;
}

// [from test_edge_duffs_device]
static void copy_bytes(char *to, const char *from, int count) {
    if (count <= 0)
        return;
    int n = (count + 7) / 8;
    switch (count % 8) {
        do {
            case 0:
                *to++ = *from++;
            case 7:
                *to++ = *from++;
            case 6:
                *to++ = *from++;
            case 5:
                *to++ = *from++;
            case 4:
                *to++ = *from++;
            case 3:
                *to++ = *from++;
            case 2:
                *to++ = *from++;
            case 1:
                *to++ = *from++;
        } while (--n > 0);
    }
}

// [from test_edge_map_macro]
#define EVAL0(...) __VA_ARGS__
#define EVAL1(...) EVAL0(EVAL0(EVAL0(__VA_ARGS__)))
#define EVAL2(...) EVAL1(EVAL1(EVAL1(__VA_ARGS__)))
#define EVAL3(...) EVAL2(EVAL2(EVAL2(__VA_ARGS__)))
#define EVAL4(...) EVAL3(EVAL3(EVAL3(__VA_ARGS__)))
#define EVAL(...)  EVAL4(EVAL4(EVAL4(__VA_ARGS__)))

#define MAP_END(...)
#define MAP_OUT
#define MAP_GET_END2()             0, MAP_END
#define MAP_GET_END1(...)          MAP_GET_END2
#define MAP_GET_END(...)           MAP_GET_END1
#define MAP_NEXT0(test, next, ...) next MAP_OUT
#define MAP_NEXT1(test, next)      MAP_NEXT0(test, next, 0)
#define MAP_NEXT(test, next)       MAP_NEXT1(MAP_GET_END test, next)
#define MAP0(f, x, peek, ...) f(x) MAP_NEXT(peek, MAP1)(f, peek, __VA_ARGS__)
#define MAP1(f, x, peek, ...) f(x) MAP_NEXT(peek, MAP0)(f, peek, __VA_ARGS__)
#define MAP(f, ...)           EVAL(MAP1(f, __VA_ARGS__, ()()(), 0))

static int sum;
#define ADD(x) sum += (x);

// [from test_edge_narg_macro]
#define _NARG(...)  _NARG_(__VA_ARGS__, _RSEQ())
#define _NARG_(...) _SEQ(__VA_ARGS__)
#define _SEQ(                                                                  \
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16,     \
    _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, \
    _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, \
    _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61, \
    _62, _63, _64, _65, _66, _67, _68, _69, _70, _71, _72, _73, _74, _75, _76, \
    _77, _78, _79, _80, _81, _82, _83, _84, _85, _86, _87, _88, _89, _90, _91, \
    _92, _93, _94, _95, _96, _97, _98, _99, _100, _101, _102, _103, _104,      \
    _105, _106, _107, _108, _109, _110, _111, _112, _113, _114, _115, _116,    \
    _117, _118, _119, _120, _121, _122, _123, _124, _125, _126, _127, N, ...)  \
    N
#define _RSEQ()                                                                \
    127, 126, 125, 124, 123, 122, 121, 120, 119, 118, 117, 116, 115, 114, 113, \
        112, 111, 110, 109, 108, 107, 106, 105, 104, 103, 102, 101, 100, 99,   \
        98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82,    \
        81, 80, 79, 78, 77, 76, 75, 74, 73, 72, 71, 70, 69, 68, 67, 66, 65,    \
        64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48,    \
        47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31,    \
        30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14,    \
        13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

_Static_assert(_NARG(a) == 1, "1 arg");
_Static_assert(_NARG(a, b) == 2, "2 args");
_Static_assert(_NARG(a, b, c) == 3, "3 args");
_Static_assert(_NARG(a, b, c, d) == 4, "4 args");
_Static_assert(_NARG(a, b, c, d, e) == 5, "5 args");

// [from test_edge_try_catch]
static jmp_buf _ex_buf;
static int     _ex_code;

#define TRY      if ((_ex_code = setjmp(_ex_buf)) == 0)
#define CATCH(e) else if (_ex_code == (e))
#define THROW(e) longjmp(_ex_buf, (e))

// [from test_edge_url_as_label_comment]
// Tests: a URL literal in function body parses as a valid C statement —
// "https:" is a goto label, "//git.sr.ht/..." is a C99 line comment.

// [from test_goto]
// Test goto and label statements
// Expected return value: 42

static int test_simple_goto() {
    int x = 0;
    goto skip;
    x = 100;  // Should be skipped
skip:
    x = x + 10;
    return x; // Should return 10
}

static int test_backward_goto() {
    int sum = 0;
    int i   = 0;
loop:
    sum = sum + i;
    i   = i + 1;
    if (i < 5)
        goto loop;
    return sum; // 0 + 1 + 2 + 3 + 4 = 10
}

static int test_forward_goto() {
    int x = 5;
    if (x > 3)
        goto end;
    x = 100;
end:
    return x; // Should return 5
}

static int test_nested_goto() {
    int result = 0;
    int i      = 0;
outer:
    if (i >= 3)
        goto done;

    int j = 0;
inner:
    if (j >= 2)
        goto next_outer;
    result = result + 1;
    j      = j + 1;
    goto inner;

next_outer:
    i = i + 1;
    goto outer;

done:
    return result; // 3 * 2 = 6
}

static int test_goto_over_code() {
    int x = 1;
    goto skip_all;

    x = x + 100;
    x = x + 200;
    x = x + 300;

skip_all:
    x = x + 2;
    return x; // Should be 3
}

// [from test_label_value_static]
// Test: &&label (labels-as-values / computed-goto) stored in a static/global
// initialiser.  Previously "undefined relocation target: .L..N" at codegen
// time (ticket #573).  apply_global_relocations() now falls back to the
// persistent label map when a relocation target is not a global object.

// Basic static local label table: jump through static void* array.

static int test_static_local(void) {
    static void *dispatch[] = {&&l_ret42, &&l_ret0};
    goto        *dispatch[0];
l_ret42:
    return 42;
l_ret0:
    return 0;
}

// File-scope label table (global static).
static void *g_tab[2];
static int   g_tab_initialized = 0;

static void init_gtab(void) {
    // Can't use &&label at file scope in an initialiser expression in C;
    // GNU C only allows &&label inside a function.  So we populate the global
    // at runtime from within a function.
    // (File-scope static init with &&label is what #573 actually tests —
    //  the static-local case above exercises the same code path.)
    (void)g_tab_initialized;
}

// Multi-label table with arithmetic offset (addend != 0 path).

static int test_label_offset(int idx) {
    static void *tab[] = {&&a, &&b, &&c};
    goto        *tab[idx];
a:
    return 10;
b:
    return 20;
c:
    return 12; // 42 - 10 - 20 = 12 (used as final accumulator below)
}

// [from test_labels_as_values]
// Test labels-as-values and computed goto (GNU extension)

// [from test_stmt_expr]
// Test statement expressions (GNU extension)
// Expected return: 42

// [from test_switch]
// Test switch statements
// Expected return: 42

static int test_simple_switch(int x) {
    switch (x) {
        case 1:
            return 10;
        case 2:
            return 20;
        case 3:
            return 30;
        default:
            return 99;
    }
}

static int test_fallthrough(int x) {
    int result = 0;
    switch (x) {
        case 1:
            result = result + 10;
        case 2:
            result = result + 20;
            break;
        case 3:
            result = result + 30;
            break;
        default:
            result = 99;
    }
    return result;
}

static int test_no_default(int x) {
    switch (x) {
        case 1:
            return 11;
        case 2:
            return 22;
    }
    return 33;
}

// [from test_switch_codegen_optimized]
// Test codegen switch lowering for dense ranges and sparse case sets

static int dense_range(int x) {
    switch (x) {
        case 0 ... 299:
            return x;
        default:
            return -1;
    }
}

static int sparse_many(int x) {
    switch (x) {
        case -1000:
            return 1;
        case -500:
            return 2;
        case -25:
            return 3;
        case 0:
            return 4;
        case 17:
            return 5;
        case 42:
            return 6;
        case 99:
            return 7;
        case 1234:
            return 8;
        case 5000:
            return 9;
        case 9000:
            return 10;
        case 12000:
            return 11;
        case 20000:
            return 12;
        default:
            return -1;
    }
}

static int sparse_no_default(int x) {
    int y = 33;
    switch (x) {
        case 10:
            y = 10;
            break;
        case 1000:
            y = 42;
            break;
        case 100000:
            y = 99;
            break;
    }
    return y;
}

// [from test_switch_debug]
// Minimal switch test with explicit values

// [from test_switch_dense_100]
// Test dense switch with 100 cases
// Expected return: 42

static int test_dense_switch(int x) {
    switch (x) {
        case 0:
            return 0;
        case 1:
            return 1;
        case 2:
            return 2;
        case 3:
            return 3;
        case 4:
            return 4;
        case 5:
            return 5;
        case 6:
            return 6;
        case 7:
            return 7;
        case 8:
            return 8;
        case 9:
            return 9;
        case 10:
            return 10;
        case 11:
            return 11;
        case 12:
            return 12;
        case 13:
            return 13;
        case 14:
            return 14;
        case 15:
            return 15;
        case 16:
            return 16;
        case 17:
            return 17;
        case 18:
            return 18;
        case 19:
            return 19;
        case 20:
            return 20;
        case 21:
            return 21;
        case 22:
            return 22;
        case 23:
            return 23;
        case 24:
            return 24;
        case 25:
            return 25;
        case 26:
            return 26;
        case 27:
            return 27;
        case 28:
            return 28;
        case 29:
            return 29;
        case 30:
            return 30;
        case 31:
            return 31;
        case 32:
            return 32;
        case 33:
            return 33;
        case 34:
            return 34;
        case 35:
            return 35;
        case 36:
            return 36;
        case 37:
            return 37;
        case 38:
            return 38;
        case 39:
            return 39;
        case 40:
            return 40;
        case 41:
            return 41;
        case 42:
            return 42;
        case 43:
            return 43;
        case 44:
            return 44;
        case 45:
            return 45;
        case 46:
            return 46;
        case 47:
            return 47;
        case 48:
            return 48;
        case 49:
            return 49;
        case 50:
            return 50;
        case 51:
            return 51;
        case 52:
            return 52;
        case 53:
            return 53;
        case 54:
            return 54;
        case 55:
            return 55;
        case 56:
            return 56;
        case 57:
            return 57;
        case 58:
            return 58;
        case 59:
            return 59;
        case 60:
            return 60;
        case 61:
            return 61;
        case 62:
            return 62;
        case 63:
            return 63;
        case 64:
            return 64;
        case 65:
            return 65;
        case 66:
            return 66;
        case 67:
            return 67;
        case 68:
            return 68;
        case 69:
            return 69;
        case 70:
            return 70;
        case 71:
            return 71;
        case 72:
            return 72;
        case 73:
            return 73;
        case 74:
            return 74;
        case 75:
            return 75;
        case 76:
            return 76;
        case 77:
            return 77;
        case 78:
            return 78;
        case 79:
            return 79;
        case 80:
            return 80;
        case 81:
            return 81;
        case 82:
            return 82;
        case 83:
            return 83;
        case 84:
            return 84;
        case 85:
            return 85;
        case 86:
            return 86;
        case 87:
            return 87;
        case 88:
            return 88;
        case 89:
            return 89;
        case 90:
            return 90;
        case 91:
            return 91;
        case 92:
            return 92;
        case 93:
            return 93;
        case 94:
            return 94;
        case 95:
            return 95;
        case 96:
            return 96;
        case 97:
            return 97;
        case 98:
            return 98;
        case 99:
            return 99;
        default:
            return -1;
    }
}

// [from test_switch_edge]
// Test switch edge cases
// Expected return: 42

static int test_default_only(int x) {
    switch (x) {
        default:
            return 42;
    }
    return 1;
}

static int test_single_case(int x) {
    switch (x) {
        case 5:
            return 42;
    }
    return 1;
}

// [from test_switch_minimal]
// Minimal switch test
// Expected return: 42

// [from test_switch_nobreak]
// Test switch statements (without break - always return)
// Expected return: 42

static int _switch_nobreak_test_simple_switch(int x) {
    switch (x) {
        case 1:
            return 10;
        case 2:
            return 20;
        case 3:
            return 30;
        default:
            return 99;
    }
    return 0; // Should never reach here
}

static int _switch_nobreak_test_no_default(int x) {
    switch (x) {
        case 1:
            return 11;
        case 2:
            return 22;
    }
    return 33;
}

#pragma cccc suite begin "control_flow"

// test_control_flow
[[cccc::test(return = 42)]]
int test_control_flow(void) {
    int result = 0;

    // Test if statement
    if (1) {
        result = result + 10; // Should execute
    }

    if (0) {
        result = result + 100; // Should NOT execute
    }

    // Test if-else
    if (5 > 10) {
        result = result + 1000; // Should NOT execute
    } else {
        result = result + 20;   // Should execute
    }

    // Test for loop - sum 1 to 5
    int i;
    for (i = 1; i <= 5; i = i + 1) {
        result = result + i;
    }
    // Adds: 1 + 2 + 3 + 4 + 5 = 15

    // Total: 10 + 20 + 15 = 45
    if (result != 45)
        return 1; // Assert result == 45
    return 42;
}

// test_edge_computed_goto_comma
[[cccc::test(return = 42)]]
int test_edge_computed_goto_comma(void) {
    void *dest = &&end;
    goto *puts("Hello world"), puts("Goodbye world"), dest;
end:
    return 42;
}

// test_edge_coroutine_switch
[[cccc::test(return = 42)]]
int test_edge_coroutine_switch(void) {
    if (counter() != 0)
        return 1;
    if (counter() != 1)
        return 2;
    if (counter() != 2)
        return 3;
    if (counter() != 3)
        return 4;
    if (counter() != 4)
        return 5;
    return 42;
}

// test_edge_duffs_device
[[cccc::test(return = 42)]]
int test_edge_duffs_device(void) {
    char src[]   = "Hello, World!12345";
    char dst[20] = {0};
    copy_bytes(dst, src, 13);
    if (memcmp(dst, "Hello, World!", 13) != 0)
        return 1;
    char dst2[20] = {0};
    copy_bytes(dst2, src, 8);
    if (memcmp(dst2, "Hello, W", 8) != 0)
        return 2;
    char dst3[20] = {0};
    copy_bytes(dst3, src, 1);
    if (dst3[0] != 'H')
        return 3;
    return 42;
}

// test_edge_map_macro
[[cccc::test(return = 42)]]
int test_edge_map_macro(void) {
    sum = 0;
    MAP(ADD, 1, 2, 3, 4, 5)
    if (sum != 15)
        return 1;
    sum = 0;
    MAP(ADD, 10, 20, 30)
    if (sum != 60)
        return 2;
    return 42;
}

// test_edge_narg_macro
[[cccc::test(return = 42)]]
int test_edge_narg_macro(void) {
    return 42;
}

// test_edge_try_catch
[[cccc::test(return = 42)]]
int test_edge_try_catch(void) {
    int caught = 0;
    TRY {
        THROW(1);
    }
    CATCH(1) {
        caught = 1;
    }
    if (caught != 1)
        return 1;

    int reached = 0;
    TRY {
        THROW(2);
        reached = 1;
    }
    CATCH(2) { /* ok */ }
    if (reached)
        return 2;

    /* nested: inner throw is caught inside outer TRY body */
    int outer = 0, inner = 0;
    TRY {
        TRY {
            THROW(3);
        }
        CATCH(3) {
            inner = 1;
        }
        outer = 1;
    }
    CATCH(99) {
        return 3;
    }
    if (!outer || !inner)
        return 4;

    return 42;
}

// test_edge_url_as_label_comment
[[cccc::test(return = 42)]]
int test_edge_url_as_label_comment(void) {
https: // git.sr.ht/~takeiteasy/cccc
    return 42;
}

// test_goto
[[cccc::test(return = 42)]]
int test_goto(void) {
    int result = 0;

    // Test 1: Simple forward goto (should return 10)
    result = test_simple_goto();
    if (result != 10) {
        return 1; // Error
    }

    // Test 2: Backward goto (loop simulation, should return 10)
    result = test_backward_goto();
    if (result != 10) {
        return 2; // Error
    }

    // Test 3: Conditional forward goto (should return 5)
    result = test_forward_goto();
    if (result != 5) {
        return 3; // Error
    }

    // Test 4: Nested gotos (should return 6)
    result = test_nested_goto();
    if (result != 6) {
        return 4; // Error
    }

    // Test 5: Goto over multiple statements (should return 3)
    result = test_goto_over_code();
    if (result != 3) {
        return 5; // Error
    }

    // All tests passed!
    return 42;
}

// test_label_value_static
[[cccc::test(return = 42)]]
int test_label_value_static(void) {
    // test_static_local: must jump to l_ret42 and return 42
    if (test_static_local() != 42)
        return 1;

    // test_label_offset: index into a 3-element static label table
    int sum =
        test_label_offset(0) + test_label_offset(1) + test_label_offset(2);
    if (sum != 42)
        return 2;

    return 42;
}

// test_labels_as_values
[[cccc::test(return = 42)]]
int test_labels_as_values(void) {
    void *target;
    void *jump_table[] = {&&a, &&b, &&c}; // Array initializer with labels
    int   index;
    int   x;
    void *next;

    // Test 1: Basic label-as-value and computed goto
    target = &&label1;
    goto *target;

    return 1; // Should not reach here

label1:
    // Test 2: Jump table using label addresses (with initializer)
    index = 1;
    goto *jump_table[index];

a:
    return 10;

b:
    // Test 3: Conditional computed goto
    x    = 5;
    next = (x > 3) ? &&success : &&fail;
    goto *next;

c:
    return 30;

fail:
    return 99;

success:
    return 42;
}

// test_stmt_expr
[[cccc::test(return = 42)]]
int test_stmt_expr(void) {
    // Test 1: Basic statement expression
    int x = ({
        int a = 10;
        int b = 32;
        a + b; // Last expression is the value
    });
    if (x != 42)
        return 1;

    // Test 2: Statement expression with control flow
    int max = ({
        int a      = 5;
        int b      = 7;
        int result = a;
        if (b > a)
            result = b;
        result;
    });
    if (max != 7)
        return 2;

    // Test 3: Nested statement expressions
    int y = ({
        int inner = ({
            int z = 20;
            z + 2;
        });
        inner + 20;
    });
    if (y != 42)
        return 3;

    return 42; // Success
}

// test_switch
[[cccc::test(return = 42)]]
int test_switch(void) {
    int result = 0;

    // Test simple cases
    if (test_simple_switch(1) != 10)
        return 1;
    if (test_simple_switch(2) != 20)
        return 2;
    if (test_simple_switch(3) != 30)
        return 3;
    if (test_simple_switch(5) != 99)
        return 4;

    // Test fallthrough
    if (test_fallthrough(1) != 30)
        return 5; // Falls through: 10 + 20
    if (test_fallthrough(2) != 20)
        return 6; // Just case 2
    if (test_fallthrough(3) != 30)
        return 7; // Just case 3
    if (test_fallthrough(9) != 99)
        return 8; // Default

    // Test no default
    if (test_no_default(1) != 11)
        return 9;
    if (test_no_default(2) != 22)
        return 10;
    if (test_no_default(5) != 33)
        return 11; // Falls through

    return 42;
}

// test_switch_codegen_optimized
[[cccc::test(return = 42)]]
int test_switch_codegen_optimized(void) {
    if (dense_range(0) != 0)
        return 1;
    if (dense_range(255) != 255)
        return 2;
    if (dense_range(299) != 299)
        return 3;
    if (dense_range(300) != -1)
        return 4;
    if (dense_range(-1) != -1)
        return 5;

    if (sparse_many(-1000) != 1)
        return 6;
    if (sparse_many(42) != 6)
        return 7;
    if (sparse_many(20000) != 12)
        return 8;
    if (sparse_many(41) != -1)
        return 9;

    if (sparse_no_default(1000) != 42)
        return 10;
    if (sparse_no_default(11) != 33)
        return 11;

    return 42;
}

// test_switch_debug
[[cccc::test(return = 42)]]
int test_switch_debug(void) {
    int x = 10;
    switch (x) {
        case 0:
            return 0;
        case 1:
            return 1;
        case 10:
            return 42; // Should match
        case 11:
            return 11;
        default:
            return 99;
    }
}

// test_switch_dense_100
[[cccc::test(return = 42)]]
int test_switch_dense_100(void) {
    // Test several cases
    if (test_dense_switch(0) != 0)
        return 1;
    if (test_dense_switch(42) != 42)
        return 2;
    if (test_dense_switch(99) != 99)
        return 3;
    if (test_dense_switch(100) != -1)
        return 4; // Default case

    return 42;    // Success
}

// test_switch_edge
[[cccc::test(return = 42)]]
int test_switch_edge(void) {
    // Test default-only switch
    if (test_default_only(1) != 42)
        return 1;
    if (test_default_only(999) != 42)
        return 2;

    // Test single case with match
    if (test_single_case(5) != 42)
        return 3;

    // Test single case without match
    if (test_single_case(3) != 1)
        return 4;

    return 42;
}

// test_switch_minimal
[[cccc::test(return = 42)]]
int test_switch_minimal(void) {
    int x = 2;

    switch (x) {
        case 1:
            return 10;
        case 2:
            return 42;
        case 3:
            return 30;
    }

    return 99;
}

// test_switch_nobreak
[[cccc::test(return = 42)]]
int test_switch_nobreak(void) {
    // Test simple cases
    if (_switch_nobreak_test_simple_switch(1) != 10)
        return 1;
    if (_switch_nobreak_test_simple_switch(2) != 20)
        return 2;
    if (_switch_nobreak_test_simple_switch(3) != 30)
        return 3;
    if (_switch_nobreak_test_simple_switch(5) != 99)
        return 4;

    // Test no default
    if (_switch_nobreak_test_no_default(1) != 11)
        return 5;
    if (_switch_nobreak_test_no_default(2) != 22)
        return 6;
    if (_switch_nobreak_test_no_default(5) != 33)
        return 7; // Falls through to return 33

    return 42;
}

// [from test_logicop_void_context]
// Regression test for #628: &&, ||, ?: side-effects in
// void/expression-statement context. Commit 4ff58d5 reworked
// ND_LOGAND/ND_LOGOR/ND_COND to reuse dest_reg as the condition scratch, but
// ND_EXPR_STMT passes REG_ZERO (hardwired zero), causing the condition to
// always read back as 0.
static int _logicop_t(const char *s) {
    fputs(s, stdout);
    return 1;
}

[[cccc::test(expect_stdout = "AOC")]]
void test_logicop_void_context(void) {
    volatile int argc = 1; // volatile: prevent constant-folding the conditions
    argc       &&_logicop_t("A");     // truthy  -> rhs runs  -> A
    argc || _logicop_t("X");          // truthy  -> short-circuit, X suppressed
    (argc - argc) || _logicop_t("O"); // 0 (falsy) -> rhs runs  -> O
    argc ? _logicop_t("C") : _logicop_t("Y"); // truthy -> then branch -> C
}

// #949: GNU elvis `a ?: b`. conditional() has two desugar paths: a plain
// ND_VAR/ND_NUM, non-volatile/non-_Atomic condition skips the compiler temp
// and builds `cond ? clone(cond) : b` directly (needed so a pure elvis
// bounds expression like `count(n ?: 8)` stays side-effect-free); anything
// else -- a side-effecting or non-trivial condition, or a volatile/_Atomic
// operand -- still goes through the original `tmp = a, tmp ? tmp : b`
// desugar. Both paths must evaluate the condition exactly once and produce
// the right value.
static int _elvis_calls = 0;
static int _elvis_side_effect(void) {
    _elvis_calls++;
    return 0;
}

[[cccc::test(return = 42)]]
int test_elvis_plain_var_fast_path(void) {
    int truthy = 7;
    if ((truthy ?: 99) != 7)
        return 1;
    int falsy = 0;
    if ((falsy ?: 99) != 99)
        return 2;
    // Fast path re-reads `truthy` for the `then` branch -- must still be 7.
    if (truthy != 7)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_elvis_side_effecting_cond_evaluated_once(void) {
    _elvis_calls = 0;
    int x        = _elvis_side_effect() ?: 5;
    if (_elvis_calls != 1)
        return 1; // must run exactly once, not twice
    if (x != 5)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_elvis_volatile_cond_uses_temp_path(void) {
    volatile int v = 3;
    if ((v ?: 99) != 3)
        return 1;
    volatile int z = 0;
    if ((z ?: 99) != 99)
        return 2;
    return 42;
}

#pragma cccc suite end
