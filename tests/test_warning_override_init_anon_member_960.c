// CCCC_FLAGS: -Woverride-init
// Ticket #960 follow-up: struct_designator()'s two -Woverride-init call
// sites print mem->name->len/loc for a designator-resolved member, which
// is NULL for an anonymous struct/union member -- a second NULL deref
// alongside #960's main crash, latent in the same code path. Both sites
// are now guarded (src/parse.c) to fall back to a nameless-member message.
//
// The warning itself does not currently fire here: overriding an
// anonymous member's field is tracked via the wrapper Initializer's
// `is_set` flag, which the designation()/TY_UNION recursion never
// actually sets to true (a separate, pre-existing gap, not part of
// #960's scope -- filed as #961). So this test's only real assertion is that
// re-designating an anonymous union field doesn't crash the compiler --
// if the is_set gap is ever fixed and the warning starts firing, the
// guarded message text ("...anonymous member") is what should appear.

struct S { union { int i; float f; }; int tag; };

int main(void) {
    struct S s = { .i = 1, .tag = 2, .i = 42 };
    return s.i;
}
