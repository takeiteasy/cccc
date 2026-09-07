// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: hello
//
// #1311: a single RunCustom command carrying BOTH an input and an output
// redirect (`cat < in > out`) used to fail outright -- command() in
// build_shell.c only ever absorbed one redirection per simple command, so
// the second one left tokens unconsumed and shell_eval_parser() rejected
// the whole command (SHELL_ERR_EVAL, exit 253, no diagnostic). Verified
// end-to-end: write a file, redirect it through `cat` into another file
// with both redirects on one command, then read the result back out.

[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "seed", "printf 'hello\\n' > cccc_test_1311_in.txt");
    RunCustom(ctx, "redirect",
              "cat < cccc_test_1311_in.txt > cccc_test_1311_out.txt");
    RunCustom(ctx, "verify", "cat cccc_test_1311_out.txt");
    return BuildDefault(ctx);
}
