// Test: cc_load_stdlib() automatically loads standard library functions
// Expected exit code: 42

#include "stdlib.h"
#include "string.h"
#include "math.h"

int main() {
    // Test string functions
    char str1[] = "Hello";
    char str2[] = "World";
    char dest[50];
    
    // These functions are available via cc_load_stdlib() called in cc_init()
    strcpy(dest, str1);
    strcat(dest, " ");
    strcat(dest, str2);
    
    if (strlen(dest) != 11) return 1;
    if (strcmp(dest, "Hello World") != 0) return 2;
    
    // Test memory functions
    char *ptr = malloc(100);
    if (!ptr) return 3;
    
    memset(ptr, 42, 100);
    if (ptr[0] != 42 || ptr[99] != 42) return 4;
    
    free(ptr);
    
    // Test conversions
    int num = atoi("123");
    if (num != 123) return 5;
    
    // Test math
    double result = sqrt(16.0);
    if (result < 3.99 || result > 4.01) return 6;
    
    // All tests passed
    return 42;
}
