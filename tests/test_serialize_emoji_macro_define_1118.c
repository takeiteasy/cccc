// Ticket #1118: -c=native auto-captures EVERY directive line from
// command-line inputs verbatim (preprocess.c push_emit_directive +
// cc_record_emit_source) and replays those lines into -m/-c=native output
// for the host compiler (cc_serialize_program's emit_directives loop). A
// non-ASCII (emoji) macro NAME -- an accepted CCCC extension -- made the
// host reject the replayed lines outright ("macro name must be an
// identifier", one per #define plus its matching #undef), failing an
// otherwise-clean native compile even though every in-AST use of the macro
// was already expanded at parse time.
//
// Fixed by dropping #define/#undef lines whose macro name starts with a
// non-ASCII byte from ordinary replay (line_macro_name_is_non_ascii,
// src/serialize.c), beside the existing per-line filters (#1003/#1054/
// #1064/#1114), gated off under --emit-cccc the same way. No known consumer
// of define replay remains post-#1114, and no other replayed directive text
// can legally reference a name the host rejects anyway.
//
// This is a serializer-only fix -- nothing changes on the plain VM path --
// so this file only keeps exiting 42 there while giving the --native suite
// (tools/tests.py --native) a standing regression guard for the shape: emoji
// macro names defined AND undefined at file scope, used in between. The
// block-scope variant lives on through tests/suites/test_suite_misc.c's
// worm/snake test, which is back on the native corpus since this fix.
// See tools/comptime_native_smoke.py case 131 for the -m-output assertion.

#define 🪱 - ~
#define 🐍 ~-
#define ascii_worm -~

#ifndef ascii_worm
#error "ASCII-named define was lost"
#endif

#ifdef ascii_worm
#undef ascii_worm
#endif

#ifndef ascii_worm
#define ascii_worm -~
#endif

int main(void) {
    // -~x == x+1, ~-x == x-1 through the emoji spellings.
    if ((🪱 42) != 43)
        return 1;
    if ((🐍 42) != 41)
        return 2;
    if ((🪱 🪱 🐍 🐍 🐍 42) != 41) // 42 +2 -3
        return 3;
    int v = 🪱 🪱 🪱 🪱 🪱 - 42;   // (-~)^5 applied to -42: -42 + 5 = -37
    if (v != -37)
        return 4;
    if ((ascii_worm 40) != 41)
        return 5;

#undef 🪱
#undef 🐍

#ifdef 🪱
#error "emoji #undef did not take effect"
#endif

    // The names are gone; plain spelling must keep working.
    if (-~41 != 42)
        return 6;
    return 42;
}
