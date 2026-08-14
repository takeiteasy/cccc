// CCCC_FLAGS: -m
// CCCC_REJECT_STDOUT: #include <stdbit\.h>
// CCCC_EXPECT_STDOUT: static unsigned int stdc_leading_zeros_ui\(unsigned int x\)
//
// #1003: <stdbit.h> is a cccc-owned polyfill with no guaranteed real system
// counterpart -- verified missing from this host's SDK, and structurally
// unreachable to a downstream compiler regardless of host, since CCCC's
// bundled include/ directory is never forwarded to it (see man/HEADERS.md).
// -c=native/-m used to replay `#include <stdbit.h>` verbatim (the same
// treatment every ordinary standard header gets) and drop the header's own
// static inline definitions (from_include), so a downstream compile failed
// with "'stdbit.h' file not found" even though CCCC itself ran the program
// fine. Fixed by marking such a header cccc-only the moment it resolves
// (is_cccc_supplied_only_header, preprocess.c), reusing the existing #896/
// #999 machinery: the #include is suppressed and the header's own content
// is re-derived instead. This test asserts on cccc's own decision (the -m
// shape), not on whether the host `cc` happens to have stdbit.h -- that
// varies by host (see stdckdint.h, which *is* present here).
#include <stdbit.h>

int main(void) { return (int)stdc_leading_zeros_ui(1u) + 11; }
