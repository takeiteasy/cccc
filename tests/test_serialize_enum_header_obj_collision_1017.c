// CCCC_FLAGS: tests/fixtures/enum_hdr_collision_1017_a.c
// tests/fixtures/enum_hdr_collision_1017_b.c -m -Wall CCCC_C4_SKIP:
// multi-source compile, not a single-TU bytecode round-trip CCCC_EXPECT_STDOUT:
// (?=[\s\S]*#include "enum_hdr_collision_1017\.h")(?=[\s\S]*static int
// AA1017;)(?=[\s\S]*static int AA1017 = 3;) CCCC_REJECT_STDOUT: __cccc_dup
// CCCC_EXPECT_STDERR: enumerator 'AA1017' is declared by an enum reached
// through a replayed #include .*enum_hdr_collision_1017\.h.* and cannot be
// renamed; the file-scope 'AA1017' declared here cannot be renamed
// either.*\[-Wnative-name-collision\]
//
// #1017: follow-up to #1016. #1016's rename_colliding_enum_constants()
// widening never renames an Obj, and #1014/#1015's own tier-1 rule never
// renames a header-exposed enumerator (the replayed #include binds it
// textually inside the header's own text) -- so when a header-exposed
// enum group's enumerator collides with a plain file-scope identifier in a
// .c that does not include that header, neither side can be renamed. The
// collision is genuinely unrepresentable in flat C by any renaming and is
// left in the generated output; without a diagnostic, the only thing the
// user ever saw was the host compiler's own "redefinition" error, which
// under -c=native names a deleted /tmp temp file with no indication which
// of their own source files was responsible or that cccc's renamer is
// even involved.
//
// Fixed not by trying to rename anything (there is no representable
// rename) but by pointing at the collision before the host compiler ever
// sees it: rename_colliding_enum_constants() (src/serialize.c) already
// computes every fact needed for this at the point it decides tier 1
// forbids the rename (the `continue` right after the #1016 comment block)
// -- the colliding Obj, its token, the enum group, and (via a new
// EnumGroup.header_path field, captured alongside header_exposed itself)
// the header that exposes it. A new warning category,
// CCCC_WARN_NATIVE_NAME_COLLISION (-Wnative-name-collision, part of
// -Wall), is emitted there via warn_tok() on the colliding Obj's own
// token, naming both the enumerator and the header. Guarded on
// obj->tok != NULL, since a comptime-synthesized Obj need not carry one
// and warn_tok() dereferences tok->file->name unconditionally.
//
// A real gap was found and fixed alongside this: cc_serialize_program()'s
// two callers (run_native_backend() for -c=native, and the -m/-c=generated
// bail-out path in main.c) never flush vm->errors after the call, so a
// warning queued during serialization itself (as opposed to during the
// earlier parse/macro-expansion phases, which do get flushed) was silently
// dropped -- confirmed by reproducing the fix with no flush added first and
// observing zero warning output despite CCCC_WARN_NATIVE_NAME_COLLISION
// firing. Both call sites now print (and, for run_native_backend, clear)
// vm->errors right after the call.
//
// This warning does not appear in tools/comptime_native_smoke.py: those
// cases assert VM 42 -> native 42, but this collision's whole point is
// that the generated output does *not* compile, so there is nothing to
// link or run. See man/COVERAGE.md's #1017 paragraph for the residual this
// leaves for the host compiler to report, and the neighbouring #1016 tests
// (tests/test_serialize_dup_enum_obj_1016.c,
// tests/test_serialize_enum_obj_no_collision_1016.c) for the
// false-positive canary proving this warning doesn't fire on cases #1016
// already fixed.
int main(void) {
    return 42;
}
