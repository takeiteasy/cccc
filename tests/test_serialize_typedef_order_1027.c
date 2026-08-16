// Ticket #1027: a struct/union whose member spells a scalar typedef name
// (e.g. `lu_byte tt;`) must not be serialized ahead of that typedef's own
// declaration under -c=native -- serialize_type_defs_for_owner used to
// print every struct/union/enum definition (ctx->defs) before any typedef
// alias (ctx->typedefs) unconditionally, two independent passes with no
// ordering between them. A large real-world corpus (tests/test_minilua.c)
// hits this within its first handful of struct definitions ("unknown type
// name 'lu_byte'"); this is the minimized repro, covering: a plain scalar
// member typedef, a typedef-of-typedef chain (TStatus -> lu_byte), and a
// typedef needed only inside an anonymous (tagless, alias-less) nested
// struct member -- which never gets its own turn in the ctx->defs loop at
// all, since that loop skips anything with no tag and no alias.

typedef unsigned char lu_byte;
typedef lu_byte TStatus;

struct GCHeader {
    struct GCHeader *next;
    lu_byte tt;
    lu_byte marked;
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
    h.next = 0;
    h.tt = 1;
    h.marked = 2;

    union Value v;
    v.tagged.tag = 3;
    v.tagged.st = 4;

    return h.tt + h.marked + v.tagged.tag + v.tagged.st + 32;
}
