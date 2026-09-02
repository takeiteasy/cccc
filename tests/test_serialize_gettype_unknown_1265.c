// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_EXPECT_STDERR: GetType: unknown type name 'NoSuchType1265'
//
// Ticket #1265: an unresolved GetType() name used to return NULL, which
// then flowed silently through MakeConst/MakeArray/GlobalVar (each just
// returns NULL on a NULL input) into malformed emitted C with no
// diagnostic. GetType() now hard-errors on an unknown name, naming the
// string and pointing at FindType()/TypeExists() as the probing entry
// points that still return NULL/false on a miss.
[[cccc::comptime]]
void gen(void) {
    GetType("NoSuchType1265");
}
gen();

int main(void) {
    return 42;
}
