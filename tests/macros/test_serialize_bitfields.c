// @serialize/@deserialize carry bit-field members and the fields of
// anonymous struct/union members.
#include <string.h>

@serialize @deserialize struct Packed {
    unsigned bits : 3;
    unsigned more : 5;
    int      plain;
    struct {
        short a;
        short b;
    };
};

int main(void) {
    struct Packed p = {5, 17, 99, {3, 4}};
    char          buf[sizeof(struct Packed)];
    memset(buf, 0xAB, sizeof buf);
    if (Packed_serialize(&p, buf) != (int)sizeof(struct Packed))
        return 1;
    struct Packed q = Packed_deserialize(buf);
    if (q.bits != 5 || q.more != 17)
        return 2;
    if (q.plain != 99 || q.a != 3 || q.b != 4)
        return 3;
    return 42;
}
