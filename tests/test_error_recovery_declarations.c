// EXPECT_COMPILE_ERROR
// Test error recovery in declarations
// Multiple declaration errors should be collected

int main() {
    // Error 1: Variable name omitted
    int , y;

    // Error 2: Another variable name omitted
    double , z;

    // Valid declaration should still parse
    int valid = 42;

    return valid;
}
