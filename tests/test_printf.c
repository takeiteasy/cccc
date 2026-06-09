// Test: printf macro works like normal C
// Returns: 0

#include "stdio.h"

int main() {
    int x = 10;
    int y = 20;
    char *name = "CCCC";
    
    printf("Hello from %s!\n", name);
    printf("x = %d, y = %d\n", x, y);
    printf("x + y = %d\n", x + y);
    
    return 42;
}
