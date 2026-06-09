/*
 * Simple demonstration that volatile, restrict, register, and inline
 * keywords are now accepted by the CCCC compiler
 */

// Inline function example
inline int add(int x, int y) {
    return x + y;
}

// Static inline (common C99/C11 pattern)
static inline int square(int x) {
    return x * x;
}

int main() {
    // Test register keyword (optimization hint)
    register int i = 0;
    register int sum = 0;
    
    // Test volatile keyword (prevents optimization)
    volatile int value = 42;
    
    // Test restrict keyword (pointer aliasing hint)
    int arr[10];
    int *restrict p = arr;
    
    // Use the inline functions
    sum = add(10, 20);       // 30
    sum = add(sum, square(5)); // 30 + 25 = 55
    
    // Use volatile variable
    sum = sum + value;  // 55 + 42 = 97
    
    // Use register variable
    for (i = 0; i < 10; i++) {
        p[i] = i;
    }
    sum = sum + p[5];  // 97 + 5 = 102

    if (sum != 102) return 1;
    return 42;
}
