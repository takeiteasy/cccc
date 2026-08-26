// #pragma comment(lib, ...) is no longer consumed as a link directive (#475).
// It is treated as an unknown pragma — warned and dropped from the token
// stream (handle_pragma_body has no `comment` branch). Use
// #pragma cccc link("name") to queue a library from source; that is also
// what -E/-m/-c=generated output now re-emits a queued library as, not this
// spelling (#1149).

// CCCC_FLAGS: -Wcpp
// CCCC_EXPECT_STDERR: unknown pragma ignored

#pragma comment(lib, "m")

int main(void) {
    return 42;
}
