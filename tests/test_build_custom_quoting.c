// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: premidpost
// CCCC_EXPECT_STDOUT: -DX="s"
// CCCC_EXPECT_STDOUT: a"b
// CCCC_EXPECT_STDOUT: a;b
// CCCC_EXPECT_STDOUT: hello world
// CCCC_EXPECT_STDOUT: a\|\|b\|
//
// RunCustom's vendored shell used to do no POSIX quote removal at all: a
// quote only mattered as the very first character of a word, and even then
// the interior was returned verbatim (embedded quotes, backslash escapes,
// and delimiters-inside-quotes were never processed). A command string like
// `-DX='"s"'` therefore reached the child with the quotes still attached,
// which is how a `cccc -c=generated ... -DSOME_MACRO='"literal"'` RunCustom
// step ended up handing the nested cccc invocation a multi-character
// character-constant instead of a string literal -- silently converted to a
// pointer and dereferenced, segfaulting the child. `echo` is external (only
// `exit`/`cd`/`pwd` are shell builtins), so its stdout is a direct readout
// of what argv the child actually received.

[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "quote1", "echo pre'mid'post");
    RunCustom(ctx, "quote2", "echo -DX='\"s\"'");
    RunCustom(ctx, "quote3", "echo \"a\\\"b\"");
    RunCustom(ctx, "quote4", "echo a\";\"b");
    RunCustom(ctx, "quote5", "echo 'hello world'");
    // '' is a standalone zero-length argument, not simply omitted.
    RunCustom(ctx, "quote6", "printf '%s|' a '' b");
    return BuildDefault(ctx);
}
