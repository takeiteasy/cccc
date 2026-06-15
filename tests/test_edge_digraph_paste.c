// Tests: %:%: digraph as ## token-paste operator in macro definitions (C23 §6.4.6)
#include <stdio.h>

#define PASTE(a, b) a %:%: b
#define XPASTE(a, b) PASTE(a, b)

int main(void) {
    int foobar = 42;
    int result = XPASTE(foo, bar);
    return result == 42 ? 42 : 1;
}
