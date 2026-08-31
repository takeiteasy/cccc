// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undefined variable 'gy'
//
// Current limitation (see "Pre-parse macro declaration context",
// man/MACROS.md): a Quote() template inside a file-scope-called comptime
// function can name a type, tag, enum constant, or function through the
// #894 demand-driven declaration index -- but not a plain file-scope
// *variable*. An object is real storage; the comptime program's data
// segment is allocated once, right after the comptime program's own parse,
// which has already finished by the time Quote() runs (comptime
// *execution* time). Splicing one in this late would have no backing slot,
// so it is refused outright rather than silently miscompiling. This must
// stay a loud compile error; a follow-up ticket may revisit making it work.

int gy = 1;

[[cccc::comptime]]
void gen(void) {
    Node *n = Quote("gy = 2;");
    (void)n;
}
gen();

int main(void) {
    return 42;
}
