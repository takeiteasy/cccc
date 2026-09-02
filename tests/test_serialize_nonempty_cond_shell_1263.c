// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: (?=[\s\S]*#ifndef LIVE_1263)(?=[\s\S]*#define LIVE_1263 "on")(?=[\s\S]*#endif)(?=[\s\S]*int live_shell_answer_1263\(void\))
//
// Ticket #1263 guard: the empty-conditional-shell drop must not touch a
// conditional whose body actually survives. Here no `-D LIVE_1263` is
// passed, so the `#define` inside the guard is kept by CCCC's preprocessor
// and the whole `#ifndef` / `#define` / `#endif` group must be replayed
// verbatim into `-c=generated` output. Companion to
// test_serialize_empty_cond_shell_1263.c.

#ifndef LIVE_1263
#define LIVE_1263 "on"
#endif

#pragma cccc comptime begin
[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("live_shell_answer_1263", GetType("int"));
    FunctionSetBody(fn, Quote("return 42;"));
    PublishNode(fn);
}
gen();
#pragma cccc comptime end

int main(void) {
    return live_shell_answer_1263();
}
