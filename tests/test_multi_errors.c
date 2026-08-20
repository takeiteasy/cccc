// EXPECT_COMPILE_ERROR
// Test file to verify error collection
// This file intentionally contains multiple errors

int main() {
    // Error 1: Undefined variable
    x = 5;

    // Error 2: Undeclared variable in expression
    int y = z + 10;

    // Error 3: Type mismatch
    int *ptr = 42;

    // Error 4: Invalid assignment to constant
    const int c = 100;
    c           = 200;

    // Error 5: Missing semicolon (syntax error)
    int a = 1 int b = 2;

    return 0;
}
