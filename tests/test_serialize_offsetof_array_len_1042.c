// Ticket #1042(d): include/stddef.h's own offsetof expansion,
// `(size_t)&(((type *)0)->member)`, is a compile-time integer constant
// expression per C11 6.6p9 -- but is_const_expr() (src/parse_analysis.c)
// had no ND_ADDR/ND_DEREF arm at all, so array_dimensions()
// (src/parse_types.c) misclassified `char padding[offsetof(T, m)]` as a
// VLA (a variable-length array). vla_of() (src/type.c) hard-codes VLA
// objects to size 8, so this was a real, live VM bug independent of
// -c=native: `sizeof` of anything containing such a member was simply
// wrong on the VM too, whenever the real offset exceeds 8. Under
// -c=native, the VLA-length replay path (serialize_type_decl's TY_VLA
// case) then re-emitted the raw expression -- including the referenced
// type's name -- with no forward-dependency tracking, producing "use of
// undeclared identifier" for a type named only inside the length
// expression.
//
// Fixed with a structural (not general "whatever eval_rval() accepts")
// ND_ADDR arm in is_const_expr(): admits exactly ND_ADDR -> (ND_MEMBER |
// ND_DEREF)* -> ND_CAST-of-ND_NUM(0), the offsetof shape and nothing
// wider (deliberately NOT `&some_global`, which eval_rval() also accepts
// but which is not itself a plain integer constant a `label`-less eval()
// call can fold). This makes the member's array length fold to a plain
// TY_ARRAY at parse time -- both the wrong sizeof AND the VLA-replay
// dependency gap disappear at the source, rather than needing two
// separate serializer patches.

#include <stddef.h>

typedef struct {
    int  a;
    long b;
    char c;
} Aux;

// The ticket's own shape: a union whose padding member's length is an
// offsetof of a DIFFERENT, earlier-declared type -- exercised through
// -c=native's TY_VLA replay path if the parser fix regresses.
typedef union {
    long lastfree;
    char padding[offsetof(Aux, c)];
} Boxed;

// A second instance nested one level deeper (member access chain, not
// just a top-level member) -- offsetof(T, a.b) desugars to a chain of two
// ND_MEMBER hops before the null-pointer base, exercising the recursive
// arm of the fix.
typedef struct {
    int inner;
    struct {
        long mid;
        char tail;
    } nested;
} Chain;

typedef struct {
    char pad[offsetof(Chain, nested.tail)];
    int  after;
} ChainBoxed;

int main(void) {
    // sizeof must reflect the REAL offset, not vla_of()'s hard-coded 8 --
    // this is the live VM bug #1042(d) also fixes, independent of
    // -c=native entirely. On every target this project supports, `int a`
    // then `long b` pads `c` out past 8 bytes (int, then alignment padding,
    // then an 8-byte long), so this offset is > 8 and a stale hard-coded-8
    // VLA size would fail this check outright.
    size_t off_c = offsetof(Aux, c);
    if (off_c <= sizeof(long))
        return 1; // the layout assumption above didn't hold -- re-check Aux
    if (sizeof(Boxed) != off_c)
        return 2;

    size_t off_tail = offsetof(Chain, nested.tail);
    if (off_tail <= sizeof(long))
        return 3; // same layout-assumption guard, for the nested chain
    if (offsetof(ChainBoxed, after) != off_tail)
        return 4;

    return 42;
}
