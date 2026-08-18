// Ticket #1064, third failure found while verifying #1018 on real Linux
// (cccc-linux-amd64 container, clang 18): -m/-c=native/-c=generated output
// used to replay a captured conditional-group directive line
// (#if/#ifdef/#ifndef/#elif/#else/#endif) verbatim -- always an empty
// shell by the time it reaches this loop, since CCCC's own preprocessor
// had already resolved which branch was taken and captured only that
// branch's own content as separate lines/directives. Replaying the shell
// anyway handed the *evaluation* to the host compiler a second time, with
// two real hazards: (1) a host lacking a feature-test macro CCCC's own
// preprocessor already resolved -- clang 18 rejects a captured
// `#if __has_embed(...)` shell outright ("function-like macro '__has_embed'
// is not defined"), see test_has_embed.c, even though CCCC evaluated it
// fine and the shell carries no content; (2) a captured `#ifdef __CCCC__`
// shell being silently false at the host (which never defines that macro),
// which would drop whatever a taken branch inside it captured -- this
// file's own shape below.
//
// Fixed by dropping conditional-group directive lines from
// cc_serialize_program()'s emit_directives replay loop (src/serialize.c),
// alongside the two existing per-line filters there (cccc-only headers,
// setjmp.h), gated off under --emit-cccc the same way.
//
// This is a serializer-only fix -- nothing here changes on the plain VM
// path, so this file's only job is to keep exiting 42 there while giving
// the --native suite (tools/tests.py --native) a standing regression guard
// for the shape: a taken #ifdef branch defining a macro a later #ifndef
// depends on. See tools/comptime_native_smoke.py case 105 for the
// -m-output-has-no-conditional-line assertion.

#ifdef __CCCC__
#define TOOK_TAKEN_BRANCH 1
#endif

#if 1
#define ALSO_TAKEN 1
#endif

#ifndef TOOK_TAKEN_BRANCH
#error "taken #ifdef __CCCC__ branch was lost"
#endif

#ifndef ALSO_TAKEN
#error "taken #if 1 branch was lost"
#endif

int main(void) {
    return 42;
}
