// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: template references \$2 but only 1 argument was passed
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
[[cccc::comptime]]
Node *probe(void) {
    return Quote("$1 + $2", MakeIntLiteral(40));
}

int main(void) {
    return probe();
}
