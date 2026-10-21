// -- a struct/union/enum referenced ONLY inside sizeof/_Alignof/a case label/an
// enum value/a _Static_assert is const-folded to a plain integer literal at
// parse time, but the operand Type is retained on the fold site (Node.layout_ty
// and friends,) -- collect_node_types()/collect_type() never walked those
// stashes, so a re-materialized `sizeof(T)`/`_Alignof(T)`
// (serialize_layout_const()) could be the only reference to T left in the whole
// AST, and no definition was emitted for it ('invalid application of sizeof to
// an incomplete type'). Fixed by walking
// Node.layout_ty/case_begin_layout_ty/case_end_layout_ty,
// Type.array_len_layout_ty, EnumConstant.layout_ty, and file-scope
// StaticAssertRecord.cond, each gated identically to serialize_layout_const()'s
// own re-materialization check (layout_type_needs_collecting()) so a type that
// stays folded is never force-emitted -- confirmed against the regression that
// gate exists for (WideBitintOnly1167, see its own comment). Covers all five
// sites plus a genuinely host-owned aggregate. Asserts VM 42 -> native 42.

#include <sys/mount.h>

// #1167: a struct/union/enum referenced ONLY inside sizeof/_Alignof/a case
// label/an enum value/a _Static_assert is const-folded to a plain integer
// literal at parse time -- collect_node_types()/collect_type()
// (serialize_type.c) never walked the operand type's own layout-provenance
// stash (Node.layout_ty and friends), so no AST node it DID walk still
// referenced it, no definition was emitted, and the host compiler rejected
// the re-materialized `sizeof(T)`/`_Alignof(T)` text as an incomplete type.
// Every struct below is deliberately referenced in exactly one of these
// five sites and nowhere else.

// Site 1: bare sizeof expression -- the ticket's own minimal repro shape
// (a plain, non-host-owned aggregate).
struct SizeOnly1167 {
    long   a;
    double b;
    char   c[3];
};

// Site 2: a LOCAL array dimension (#1095's own array-dimension gate only
// re-materializes a local's or an uninitialized global's own declarator).
struct ArrOnly1167 {
    int  x;
    long y;
};

// Site 3: a `case` label.
struct CaseOnly1167 {
    char a;
    int  b;
};

// Site 4: an enum value.
struct EnumOnly1167 {
    long a;
    long b;
    long c;
};

// #1167: legitimately host-owned (wraps a from_include aggregate,
// `struct statfs`), referenced only via sizeof -- distinct from the
// spurious scalar-vs-from_include-typedef gate misfire that
// type_layout_is_host_owned() can also trigger (a separate, out-of-scope
// defect); this one really is host-owned per that function's own by-value
// -member walk, so it must both re-materialize AND actually get a real
// definition emitted.
struct WrapStatfs1167 {
    struct statfs s;
};

// #1167 regression guard: a member type with NO native/-m lowering at all
// (`_BitInt(129)` -- serialize_type.c's own TY_BITINT arm hard-errors on
// it), referenced only via sizeof, and NOT host-owned -- must stay folded
// (never actually emitted), exactly as it did before this fix. An earlier,
// unconditional version of this fix collected every layout_ty regardless
// of whether serialize_layout_const() would ever re-materialize it,
// forcing this struct's body into the output and hitting that hard error
// even though nothing about it is host-owned (caught by
// tests/suites/test_suite_structs.c's own `tc_bi1135_wide`).
struct WideBitintOnly1167 {
    char c;
    _BitInt(129) x;
};

// Site 5: _Static_assert, non-host-owned, both file- and block-scope.
struct AssertOnly1167 {
    long a;
    long b;
};
_Static_assert(sizeof(struct AssertOnly1167) == sizeof(struct AssertOnly1167),
               "file-scope AssertOnly1167");

int main(void) {
    long sizeof_expr = (long)sizeof(struct SizeOnly1167);

    char arr[sizeof(struct ArrOnly1167)];
    (void)arr;

    int v = (int)sizeof(struct CaseOnly1167);
    switch (v) {
        case sizeof(struct CaseOnly1167):
            break;
        default:
            return 1;
    }

    enum { N1167 = sizeof(struct EnumOnly1167) };
    if (N1167 <= 0)
        return 2;

    long wrap_sz   = (long)sizeof(struct WrapStatfs1167);
    long bitint_sz = (long)sizeof(struct WideBitintOnly1167);

    static_assert(sizeof(struct AssertOnly1167) ==
                      sizeof(struct AssertOnly1167),
                  "block-scope AssertOnly1167");

    if (sizeof_expr > 0 && wrap_sz > 0 && bitint_sz > 0)
        return 42;
    return 3;
}
