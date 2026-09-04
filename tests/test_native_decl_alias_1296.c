// -c=native regression (#1296): a bodiless declaration reached through an
// uncaptured bundled header (see native_decl_alias_1296.h's own comment)
// used to decompose a real SDK-typed parameter to CCCC's own guessed
// shape, colliding with the SDK's real declaration of the same function.
// Two shapes:
//
//   - setpriority/getpriority: CCCC's own include/sys/resource.h declared
//     `int who`; both macOS and glibc declare `id_t who`
//     ("conflicting types for 'setpriority'"/'getpriority'"). Fixed by
//     spelling the parameter `id_t` in the header itself.
//   - catopen/catgets/catclose: the #1096 fallback prototype pass
//     decomposed the `nl_catd` pointer typedef to its underlying pointer
//     type (`void *catd`) instead of keeping the alias, colliding with the
//     SDK's `nl_catd`-typed declaration ("conflicting types for
//     'catclose'"/'catgets'/'catopen'"). Fixed by preserving a from_include
//     pointer typedef's alias when serializing a bodiless declaration's
//     parameter/return types (serialize_aliased_ptr_type_decl,
//     src/serialize_type.c).
//
// setpriority()/catopen()/catgets()/catclose() are only referenced (never
// invoked) behind an always-false volatile guard -- is_used still fires at
// parse time, so their prototypes are still emitted and still exercise the
// bug, without this test actually touching process priority or opening a
// message catalog. getpriority() is harmless to call for real.
//
// A CCCC bundled header reached through this companion header's own
// #include is now (#1297) treated as captured -- the host compiler
// follows this header's own replayed #include straight through to the
// real <sys/resource.h>/<nl_types.h>, so the fallback prototype this test
// used to exercise no longer fires here at all. This program now instead
// proves the replayed real declarations alone are enough for a native
// build to accept every one of these calls; the id_t-parameter and
// alias-preservation fixes themselves stay covered by
// tools/comptime_native_smoke.py's case_bundled_chain_prototype_still_
// emitted_1297 and case_bundled_ptr_typedef_alias_1296 (bundled->bundled
// chains #1297 deliberately leaves uncaptured).
#include "native_decl_alias_1296.h"

static volatile int g_never = 0;

int main(void) {
    int p = getpriority(PRIO_PROCESS, 0);
    (void)p;

    if (g_never) {
        setpriority(PRIO_PROCESS, 0, 0);
        nl_catd c = catopen("x", 0);
        catgets(c, 1, 1, "hi");
        catclose(c);
    }

    return 42;
}
