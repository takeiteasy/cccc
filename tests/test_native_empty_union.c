// -c=native regression (#1115): global initializers of empty (0-byte) unions
// used to hit "cannot serialize initializer ... union has no member spanning
// the full 0-byte object". The serializer now emits an empty brace
// initializer for them; normal unions keep taking the largest-member path.
#include <stdio.h>

union {
} misc_empty_global      = {};
union {
} misc_empty_global_arr[3] = {};
union {
    int a;
} normal_union           = {.a = 7};

int main(void) {
    union {
    } local_empty = {};
    (void)local_empty;
    if (normal_union.a != 7)
        return 1;
    printf("empty unions ok\n");
    return 42;
}
