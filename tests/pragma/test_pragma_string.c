// Test pragma macro with string literal generation

#include <string.h>

// Define a pragma macro that generates a string literal
#pragma macro
_Node *make_hello(void) {
    return __jcc_ast_string_literal(__jcc_get_vm(), "Hello, World!");
}

int main(void) {
    const char *msg = make_hello();

    if (strcmp(msg, "Hello, World!") != 0) {
        return 1;
    }

    return 42;
}
