// -c=native, a struct/union member spelling a scalar typedef name (e.g.
// `lu_byte tt;`) no longer serializes ahead of that typedef's own declaration
// -- serialize_type_defs_for_owner used to print every struct/union/enum
// definition before any typedef alias unconditionally, two independent passes
// with no ordering between them (; tests/test_minilua.c, a real-world corpus,
// hit this within its first handful of struct definitions, 'unknown type name
// lu_byte'). Covers a typedef-of-typedef chain (TStatus -> lu_byte) and a
// typedef needed only inside an anonymous nested struct member, which never
// gets its own turn in the top-level struct/union/enum loop at all (no tag, no
// alias, nothing to refer back to it by). Asserts VM 42 -> native 42

typedef unsigned char lu_byte;
typedef lu_byte       TStatus;

struct GCHeader {
    struct GCHeader *next;
    lu_byte          tt;
    lu_byte          marked;
};

union Value {
    struct {
        lu_byte tag;
        TStatus st;
    } tagged;
    int plain;
};

int main(void) {
    struct GCHeader h;
    h.next   = 0;
    h.tt     = 1;
    h.marked = 2;

    union Value v;
    v.tagged.tag = 3;
    v.tagged.st  = 4;

    return h.tt + h.marked + v.tagged.tag + v.tagged.st + 32;
}
