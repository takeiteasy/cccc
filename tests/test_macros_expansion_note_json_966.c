// CCCC_FLAGS: -j
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: "notes":\[\{"file":
// CCCC_EXPECT_STDERR: "message":"in expansion of macro 'inner' at
// Ticket #966: under -j (JSON diagnostics), a comptime expansion backtrace
// is rendered as a "notes" array on the diagnostic object rather than as
// plain-text "note:" lines.

[[cccc::comptime]]
Node *inner(void) {
    return Quote("undefined_thing_inner");
}

[[cccc::comptime]]
Node *outer(void) {
    return Quote("inner()");
}

int main(void) {
    return outer();
}
