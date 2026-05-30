// Test basic pragma macro functionality
// A macro that returns a constant integer literal

#include <reflection.h>

// Define a pragma macro that generates an integer literal
#pragma macro
JCC_Node *make_five(void) { return jcc_ast_int_literal(jcc_get_vm(), 5); }

// Use the macro in code
int main(void) {
    int x = make_five();

    if (x != 5) {
        return 1;
    }

    // Test using macro in expression
    int y = make_five() + 10;
    if (y != 15) {
        return 2;
    }

    return 42;
}
