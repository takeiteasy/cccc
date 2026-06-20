// CCCC_FLAGS: --testing
// Regression test for nexttoward / nextafter family.
// nexttoward(double, long double) and nexttowardf(float, long double) have a 128-bit
// second argument on Linux/aarch64; the VM redirects them to nextafter/nextafterf to
// avoid the host ABI mismatch (#491).  This test confirms correct directional behaviour
// on all platforms.
#include <math.h>

[[cccc::test]] void test_nextafter_basic(void) {
    double a = nextafter(1.0, 2.0);
    Assert(a > 1.0);
    double b = nextafter(1.0, 0.0);
    Assert(b < 1.0);
    // stepping in same direction twice moves further
    Assert(nextafter(a, 2.0) > a);
}

[[cccc::test]] void test_nextafter_float(void) {
    float a = nextafterf(1.0f, 2.0f);
    Assert(a > 1.0f);
    float b = nextafterf(1.0f, 0.0f);
    Assert(b < 1.0f);
}

[[cccc::test]] void test_nexttoward_matches_nextafter(void) {
    // nexttoward(x, y) with double y should give the same step as nextafter(x, y).
    double x = 1.0;
    double y = 2.0;
    Assert(nexttoward(x, y) == nextafter(x, y));
    Assert(nexttoward(x, 0.0) == nextafter(x, 0.0));
}

[[cccc::test]] void test_nexttowardf_matches_nextafterf(void) {
    float x = 1.0f;
    Assert(nexttowardf(x, 2.0f) == nextafterf(x, 2.0f));
    Assert(nexttowardf(x, 0.0f) == nextafterf(x, 0.0f));
}

[[cccc::test]] void test_nexttowardl_matches_nextafter(void) {
    double x = 1.0;
    // nexttowardl(long double, long double) also redirects to nextafter (#491)
    long double a = nexttowardl((long double)x, (long double)2.0);
    Assert(a > (long double)1.0);
    long double b = nexttowardl((long double)x, (long double)0.0);
    Assert(b < (long double)1.0);
}
