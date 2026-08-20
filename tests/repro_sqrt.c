#include "math.h"
#include "stdio.h"

int main() {
    double x      = 16.0;
    double result = sqrt(x);
    printf("sqrt(16.0) = %f\n", result);
    // sqrt(16.0) should be 4.0
    if (result < 3.99)
        return 1; // Too small
    if (result > 4.01)
        return 2; // Too large
    return 42;
}
