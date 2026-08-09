// Test FFI (Foreign Function Interface) calls to C standard library
// Expected return: 42

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "math.h"

// Test string functions
int test_string_functions() {
    char str1[20];
    char str2[20];
    
    // Test strcpy
    strcpy(str1, "hello");
    
    // Test strcmp
    if (strcmp(str1, "hello") != 0) return 1;
    
    // Test strlen
    if (strlen(str1) != 5) return 2;
    
    // Test strcat
    strcpy(str2, " world");
    strcat(str1, str2);
    if (strcmp(str1, "hello world") != 0) return 3;
    
    return 0;
}

// Test memory functions
int test_memory_functions() {
    // Test malloc/free
    void *vptr = malloc(40);  // 8 bytes per int * 5 ints
    int *ptr = vptr;
    if (!ptr) return 4;
    
    ptr[0] = 10;
    ptr[1] = 20;
    ptr[2] = 30;
    
    int sum = ptr[0] + ptr[1] + ptr[2];  // Should be 60
    
    free(ptr);
    
    if (sum != 60) return 5;
    
    // Test memset
    char buffer[10];
    memset(buffer, 0, 10);
    if (buffer[0] != 0 || buffer[9] != 0) return 6;
    
    // Test memcpy
    char src[5] = {1, 2, 3, 4, 5};
    char dst[5];
    memcpy(dst, src, 5);
    if (dst[2] != 3) return 7;
    
    // Test memcmp
    if (memcmp(src, dst, 5) != 0) return 8;
    
    return 0;
}

// Test conversion functions
int test_conversions() {
    // Test atoi
    int val = atoi("123");
    if (val != 123) return 9;
    
    // Test atol
    long lval = atol("456");
    if (lval != 456) return 10;
    
    return 0;
}

// Test math functions
int test_math() {
    // Test sqrt
    double result = sqrt(16.0);
    if (result < 3.99 || result > 4.01) return 11;
    
    // Test pow
    double power = pow(2.0, 3.0);
    if (power < 7.99 || power > 8.01) return 12;
    
    // Test floor/ceil
    double f = floor(3.7);
    if (f < 2.99 || f > 3.01) return 13;
    
    double c = ceil(3.2);
    if (c < 3.99 || c > 4.01) return 14;
    
    return 0;
}

int main() {
    int result;
    
    puts("Testing string functions...");
    result = test_string_functions();
    if (result != 0) {
        puts("String test failed!");
        return result;
    }
    
    puts("Testing memory functions...");
    result = test_memory_functions();
    if (result != 0) {
        puts("Memory test failed!");
        return result;
    }
    
    puts("Testing conversions...");
    result = test_conversions();
    if (result != 0) {
        puts("Conversion test failed!");
        return result;
    }
    
    puts("Testing math...");
    result = test_math();
    if (result != 0) {
        puts("Math test failed!");
        return result;
    }
    
    puts("All tests passed!");
    return 42;  // All tests passed
}
