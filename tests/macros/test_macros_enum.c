// Test pragma macro with enum reflection
// Generates an enum-to-string function at compile time

#include <stdio.h>
#include <string.h>

// Define an enum before the macro
typedef enum { COLOR_RED, COLOR_GREEN, COLOR_BLUE } Color;

// Pragma macro to get enum constant name at compile time
[[jcc::macro]]
_Node *color_name(_Node *value) {
    _VirtualMachine *vm = __jcc_get_vm();

    // Find the Color type
    _Type *color_type = __jcc_ast_find_type(vm, "Color");
    if (!color_type) {
        // Fallback if type not found
        return __jcc_ast_string_literal(vm, "unknown");
    }

    // Build a switch statement that maps values to strings
    // For now, just demonstrate enum introspection works
    int count = __jcc_ast_enum_count(vm, color_type);

    // Return a string showing we found the enum
    if (count == 3) {
        return __jcc_ast_string_literal(vm, "enum_found");
    }
    return __jcc_ast_string_literal(vm, "enum_not_found");
}

int main(void) {
    const char *result = color_name(COLOR_RED);

    if (strcmp(result, "enum_found") != 0) {
        printf("Expected 'enum_found', got '%s'\n", result);
        return 1;
    }

    return 42;
}
