#include "stdio.h"

double add(double a, double b) {
    printf("add: a=%f, b=%f\n", a, b);
    return a + b;
}

int main() {
    // Test 4: Nested function calls - this works
    double nested = add(2.0 * 20.0, 2.0); // (2*20) + 2 = 42
    printf("nested = %f\n", nested);
    if (nested != 42.0)
        return 4;

    // Test 5: Variable arguments - this fails
    double x = 10.0;
    double y = 32.0;
    printf("Before add: x=%f, y=%f\n", x, y);
    double sum = add(x, y);
    printf("sum = %f (expected 42.0)\n", sum);
    if (sum != 42.0)
        return 5;

    return 42;
}
