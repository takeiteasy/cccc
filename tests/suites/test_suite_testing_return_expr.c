// CCCC_FLAGS: --testing
// Suite: return= operands that are names, macros and constant expressions.

#include <stddef.h>

#pragma cccc suite begin "return_expr"

enum Color { RED, GREEN, BLUE };

#define ANSWER   42
#define TWICE(x) ((x) * 2)
#define GREETING "hello"
#define ORIGIN                                                                 \
    (struct Point) {                                                           \
        .x = RED, .y = TWICE(GREEN)                                            \
    }

struct Point {
    int x;
    int y;
};

struct Node {
    int          val;
    struct Node *next;
    int (*hook)(void);
};

struct Shape {
    struct Point at;
    double       scale;
};

[[cccc::test(return = GREEN)]]
int test_enum_name(void) {
    return GREEN;
}

[[cccc::test(return != RED)]]
int test_enum_name_ne(void) {
    return BLUE;
}

[[cccc::test(return >= GREEN)]]
int test_enum_name_ordered(void) {
    return BLUE;
}

[[cccc::test(return = -GREEN)]]
int test_negated_enum(void) {
    return -1;
}

[[cccc::test(return = ANSWER)]]
int test_object_macro(void) {
    return 42;
}

[[cccc::test(return = TWICE(BLUE))]]
int test_function_macro(void) {
    return 4;
}

[[cccc::test(return = 6 * 7)]]
int test_arithmetic(void) {
    return 42;
}

[[cccc::test(return = (int)sizeof(long))]]
int test_sizeof(void) {
    return 8;
}

[[cccc::test(return = 'A')]]
int test_char_literal(void) {
    return 65;
}

[[cccc::test(return = 0xBEEF)]]
int test_hex_literal(void) {
    return 0xBEEF;
}

[[cccc::test(return = 0xE)]]
int test_hex_literal_with_e(void) {
    return 14;
}

[[cccc::test(return = 1.5f)]]
double test_float_suffix(void) {
    return 1.5;
}

[[cccc::test(return = -1.5)]]
double test_negative_float(void) {
    return -1.5;
}

[[cccc::test(return = 1.0 / 4)]]
double test_float_expression(void) {
    return 0.25;
}

[[cccc::test(return != 99.0)]]
double test_float_ne(void) {
    return 1.0;
}

[[cccc::test(return < 2.0)]]
double test_float_lt(void) {
    return 1.0;
}

[[cccc::test(return <= 1.0)]]
double test_float_le(void) {
    return 1.0;
}

[[cccc::test(return > 0.5)]]
double test_float_gt(void) {
    return 1.0;
}

[[cccc::test(return >= 1.0)]]
double test_float_ge(void) {
    return 1.0;
}

[[cccc::test(return = GREETING)]]
const char *test_string_macro(void) {
    return "hello";
}

[[cccc::test(return = (struct Point){.x = GREEN, .y = ANSWER})]] struct Point
    test_struct_field_names(void) {
    return (struct Point){1, 42};
}

[[cccc::test(return = ORIGIN)]]
struct Point test_struct_macro(void) {
    return (struct Point){0, 2};
}

[[cccc::test(return = (struct Shape){.at    = {.x = BLUE, .y = 6 * 7},
                                     .scale = -0.5})]] struct Shape
    test_nested_struct_field_expressions(void) {
    return (struct Shape){{2, 42}, -0.5};
}

static struct Node tail_node;
static int hook_fn(void) {
    return 0;
}

[[cccc::test(return = (struct Node){.val  = 1,
                                    .next = NULL,
                                    .hook = nullptr})]] struct Node
    test_pointer_fields_null(void) {
    return (struct Node){1, NULL, NULL};
}

[[cccc::test(return = (struct Node){.val = 1,
                                    .next != NULL,
                                    .hook != NULL})]] struct Node
    test_pointer_fields_non_null(void) {
    return (struct Node){1, &tail_node, hook_fn};
}

[[cccc::test(return = (struct Node){.val != 2, .next = 0})]] struct Node
    test_field_operator_on_int(void) {
    return (struct Node){1, NULL, NULL};
}

[[cccc::test(return != NULL)]]
int *test_pointer_return_non_null(void) {
    return &tail_node.val;
}

[[cccc::test(return = NULL)]]
int *test_pointer_return_null(void) {
    return NULL;
}

#pragma cccc suite end
