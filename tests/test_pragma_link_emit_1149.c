// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: #pragma cccc link\("m"\)
// CCCC_REJECT_STDOUT: comment\(lib

// A library queued via `#pragma cccc link("m")` used to be re-emitted into
// -m/-c=generated output as `#pragma comment(lib, "m")` -- a spelling that
// looks portable but isn't an input form cccc itself understands
// (handle_pragma_body, src/preprocess.c, has no `comment` branch), so
// re-feeding the generated output back to cccc silently dropped the link
// requirement (#1149). Now re-emitted as `#pragma cccc link("m")`, which IS
// parsed back on input by the same handler, so the queue round-trips.

#pragma cccc link("m")

extern double sqrt(double x);

int main(void) {
    return (int)sqrt(1764.0) == 42 ? 42 : 1;
}
