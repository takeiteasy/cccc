// EXPECT_COMPILE_ERROR
// CCCC_REJECT_STDERR: expected '>' after #embed filename
// Offline regression test: the tokenizer must preserve "//" inside an
// #embed <URL> path (it used to only do so for #include, so the URL was
// truncated at the first // and the directive died with "expected '>').
// The compile must still fail, but for the right reason -- either
// "failed to fetch URL" (curl build; port 1 on loopback refuses instantly)
// or "URL embeds require ... CCCC_HAS_CURL=1" (plain build) -- never the
// tokenizer error rejected above.
//
// clang-format off: clang-format rewrites "//" inside an angle-bracket path
// into a line comment ("http://" becomes "http: //"), which would break the
// URL and silently defeat this test's purpose.
int main(void) {
    static const unsigned char data[] = {
#embed <http://127.0.0.1:1/no_such_file.bin>
    };
    return data[0] == 0 ? 42 : 1;
}
// clang-format on
