// CCCC_FLAGS: -Woverride-init
// CCCC_EXPECT_STDERR: initializer overrides prior initialization of 'i'.*\[-Woverride-init\]
// Ticket #960 follow-up: struct_designator()'s two -Woverride-init call
// sites used to print mem->name->len/loc for a designator-resolved member,
// which is NULL for an anonymous struct/union member -- a second NULL
// deref alongside #960's main crash, latent in the same code path. Both
// sites are guarded (src/parse.c) to skip the wrapper-level check for an
// anonymous member entirely, deferring to the recursive designation()
// call that resolves the real leaf field.
//
// #961 follow-up: the warning did not fire here at all, and the ticket's
// diagnosis (an anonymous-member `is_set` propagation gap) turned out to
// be only half right -- the anonymous-*struct* case already worked. The
// actual root cause was that designation()'s TY_UNION branch performed no
// override check whatsoever, which also affected plain *named* nested
// unions, not just anonymous ones (see
// test_warning_override_init_union_nested.c). Fixed by adding the check
// there (union members alias, so a different-member override warns too,
// see test_warning_override_init_union_diff_member.c), naming the leaf
// field ('i') once it's resolved at the wrapper's own level.

struct S { union { int i; float f; }; int tag; };

int main(void) {
    struct S s = { .i = 1, .tag = 2, .i = 42 };
    return s.i;
}
