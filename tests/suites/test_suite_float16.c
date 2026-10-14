// CCCC_FLAGS: --testing
// _Float16 is storage-only: declarations, layout and pointers work.

#include <stddef.h>
#include <string.h>

typedef _Float16 half;

struct Packed {
    char     c;
    _Float16 h;
    int      i;
    half     hs[3];
};

extern _Float16 never_called(_Float16 x, _Float16 *out);

[[cccc::test(return = 42)]]
int test_size_and_alignment(void) {
    if (sizeof(_Float16) != 2 || _Alignof(_Float16) != 2)
        return 1;
    if (sizeof(half) != 2)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_struct_layout(void) {
    if (offsetof(struct Packed, h) != 2)
        return 1;
    if (offsetof(struct Packed, i) != 4)
        return 2;
    if (offsetof(struct Packed, hs) != 8 || sizeof(struct Packed) != 16)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_raw_bits_through_pointer(void) {
    _Float16       h;
    _Float16      *p    = &h;
    unsigned short bits = 0x3c00;
    unsigned short back = 0;
    memcpy(p, &bits, sizeof(bits));
    memcpy(&back, &h, sizeof(back));
    return back == 0x3c00 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_generic_selection(void) {
    _Float16 h;
    if (_Generic(h, _Float16: 1, default: 0) != 1)
        return 1;
    if (_Generic(&h, _Float16 *: 1, default: 0) != 1)
        return 2;
    if (_Generic(h, float: 1, default: 0) != 0)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_zero_initializers(void) {
    struct Packed        p    = {0};
    _Float16             a[4] = {0};
    static struct Packed g    = {0};
    unsigned short       raw;
    memcpy(&raw, &p.h, sizeof(raw));
    if (raw != 0 || p.i != 0 || sizeof(a) != 8)
        return 1;
    memcpy(&raw, &g.h, sizeof(raw));
    return raw == 0 ? 42 : 2;
}

[[cccc::test(return = 42)]]
int test_struct_copy(void) {
    struct Packed a = {0};
    a.i             = 42;
    struct Packed b = a;
    return b.i;
}
