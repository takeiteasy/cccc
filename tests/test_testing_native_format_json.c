// CCCC_FLAGS: --testing=native --test-format=json
// CCCC_EXPECT_STDERR: \{"name":"passes","status":"pass"\}
// CCCC_EXPECT_STDERR: \{"name":"fails","suite":"math","status":"fail","message":"
// CCCC_REJECT_STDERR: TAP version
// --test-format=json prints the VM runner's JSON array.
[[cccc::test]]
void passes(void) {}

#pragma cccc suite begin "math"
[[cccc::test]]
void fails(void) {
    Assert(0);
}
#pragma cccc suite end
