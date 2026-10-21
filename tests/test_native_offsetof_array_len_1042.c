// An array length written with offsetof round-trips under -c=native.

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
