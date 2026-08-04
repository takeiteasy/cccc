// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: initialized global in the runtime translation unit
// Ticket #893: an initialized global whose initializer is not a
// self-contained constant (here, the address of another global) is still
// dropped from the comptime declaration snapshot -- but referencing it from
// a [[cccc::comptime]] body now gets a targeted hint instead of a bare
// "undefined variable", pointing at [[cccc::comptime]] variables (#188) or
// #define @shared as the supported ways to make the value visible.

static int other = 5;
static int *p = &other;

[[cccc::comptime]]
void gen(void) {
    (void)p;
}
gen();

int main(void) { return 42; }
