// Ticket #1063: found while verifying #1018's va_start/va_arg/va_copy/
// va_end translation through the real cccc-linux-amd64 container -- the
// first time this batch's verification had gone past macOS.
// tools/tests.py --native always forwards -I./include to the native cc
// (same hazard #1054 documented for setjmp.h), so CCCC's own bundled
// include/math.h wins the header search over the real host <math.h>.
//
// #1052 added four native_accessor_shims entries (src/serialize.c) --
// __cccc_issignaling_f/_d and __cccc_iseqsig_f/_d -- each emitted as a
// `static` definition once issignaling()/iseqsig() is actually used, but
// left their extern declarations in include/math.h unguarded. Under
// -c=native with -I./include forwarded, the host compiler sees the
// unguarded `int __cccc_issignaling_f(float);` extern first (this
// header), then the static definition serialize.c emits -- "static
// declaration of '__cccc_issignaling_f' follows non-static declaration".
// #1021 already fixed the identical trap for isnan_f/isinf_f/signbit_f/
// fpclassify_f (include/math.h:56-67, guarded on __CCCC__) and #1023 for
// __cccc_errno_ptr; #1052's four names were the only ones left unguarded.
//
// Fixed by wrapping the four declarations (include/math.h:535-541) in
// `#ifdef __CCCC__`, exactly like the isnan/isinf/signbit/fpclassify
// block right above them. The shim's own static body (always emitted
// ahead of every use, serialize_native_accessor_shims()) is a complete
// definition that also serves as its own prototype, so dropping the
// extern under __CCCC__ costs nothing -- an ordinary #include_next-served
// build (no __CCCC__) still sees whatever the real host <math.h> declares
// (or doesn't -- issignaling/iseqsig are still C23-only there too).
//
// Deliberately does NOT use the C23 fmaximum/fminimum/totalorder family
// (#1037's macOS libm gap) or need <fenv.h>'s feraiseexcept side effect
// checked -- just enough to exercise both shim pairs. Round-trips VM 42 ->
// native 42 on every supported platform once fixed; pre-fix, this file
// hard-fails to compile under `cccc -I./include -c=native` with the exact
// "static declaration ... follows non-static declaration" error (confirmed
// against the unpatched binary before landing this fix).

#include <math.h>

int main(void) {
    float  sf = 0.0f / 0.0f; // a NaN; not asserted signaling either way
    double sd = 0.0 / 0.0;

    // Just exercise both dispatch arms (float/double) of each macro --
    // the point is that the program compiles and links natively at all,
    // not any particular classification result.
    int a = issignaling(sf);
    int b = issignaling(sd);
    int c = iseqsig(1.0f, 1.0f);
    int d = iseqsig(1.0, 1.0);

    if (a != 0 && a != 1)
        return 1;
    if (b != 0 && b != 1)
        return 2;
    if (!c)
        return 3;
    if (!d)
        return 4;

    return 42;
}
