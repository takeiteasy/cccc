// CCCC_FLAGS: -E
// CCCC_EXPECT_STDOUT: #pragma cccc link\("m"\)
// CCCC_REJECT_STDOUT: comment\(lib

// Same as test_pragma_link_emit_1149.c but for -E (preprocessed output),
// the other site cc_output_preprocessed re-emits a queued
// `#pragma cccc link("m")` -- also now CCCC's own round-tripping spelling,
// not `#pragma comment(lib, "m")` (#1149).

#pragma cccc link("m")

extern double sqrt(double x);

int main(void) {
    return (int)sqrt(1764.0) == 42 ? 42 : 1;
}
