// -c=native, #include <stdbit.h> (a cccc-owned polyfill with no guaranteed real
// system counterpart) resolved from the embedded src/std_table.c table -- since
// this runs from a temp cwd, there is no on-disk ./include -- rounds trip VM 42
// -> native 42 instead of the host compiler failing with 'stdbit.h' file not
// found. tests/test_serialize_polyfill_header_not_replayed.c covers the -m
// shape and the on-disk resolution branch (the test targets always pass
// -I./include); this case is the one that actually exercises the embedded
// branch, per the lesson that the two resolution paths need separate coverage
#include <stdbit.h>
int main(void) {
    return (int)stdc_leading_zeros_ui(1u) + 11;
}
