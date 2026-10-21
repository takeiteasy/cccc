// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: char padding\[16\];
// CCCC_REJECT_STDOUT: padding\[\(
// CCCC_REJECT_STDOUT: padding\[&
// -m output prints the array length as a constant.

#include <stddef.h>

typedef struct {
    int  a;
    long b;
    char c;
} Aux;

typedef union {
    long lastfree;
    char padding[offsetof(Aux, c)];
} Boxed;

int main(void) {
    if (sizeof(Boxed) != offsetof(Aux, c))
        return 1;
    if (offsetof(Aux, c) <= sizeof(long))
        return 2;
    return 42;
}
