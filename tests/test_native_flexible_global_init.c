// A global or static initializing a flexible array member keeps the declared
// `T tail[]` struct type under -c=native: sizeof and offsets match the host,
// and every global of the type works whatever its tail length.
#include <stddef.h>

struct F {
    int       n;
    long long x;
    int       tail[];
};

typedef struct {
    char  c;
    short tail[];
} Packet;

struct F gshort = {1, 2, {7}};
struct F glong  = {2, 5, {10, 20, 30}};
Packet   gpkt   = {'p', {4, 5}};

int main(void) {
    static struct F s = {3, 9, {1, 2, 3, 4}};
    if (offsetof(struct F, tail) != 16)
        return 1;
    if (gshort.tail[0] != 7)
        return 2;
    if (glong.tail[0] != 10 || glong.tail[1] != 20 || glong.tail[2] != 30)
        return 3;
    if (gpkt.tail[0] != 4 || gpkt.tail[1] != 5)
        return 4;
    if (s.tail[3] != 4 || s.x != 9)
        return 5;
    return 42;
}
