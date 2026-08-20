// #pragma comment(lib, ...) is no longer consumed as a link directive (#475).
// It is treated as an unknown pragma — warned and passed through in
// -c=generated output. Use #pragma cccc link("name") to queue a library from
// source.

// CCCC_FLAGS: -Wcpp
// CCCC_EXPECT_STDERR: unknown pragma ignored

#pragma comment(lib, "m")

int main(void) {
    return 42;
}
