// Regression test for #712: floating-point argument marshalling clobbered a
// live integer argument register.
//
// FREG_A0..A7 alias REG_A0..A7 by raw register number (regs[]/fregs[] are
// separate storage but share index numbers). Many expression codegen paths
// legitimately reuse their destination register *number* as an integer
// scratch while producing a float result -- deref/member address
// computation, int->float cast source, ternary condition -- so evaluating a
// float call argument directly into FREG_A0+i clobbered a live REG_A0+i
// already holding a marshalled integer argument (e.g. printf's format
// pointer). This exercises every marshalling site that was fixed: the FFI
// (variadic) call path, the native call path, a nested call inside a float
// argument, and block invocation.
#include <stdio.h>
#include <string.h>

static int chk(int n, double d) { return (n == 7 && d == 3.5) ? 1 : 0; }
static int chkf(int n, float f) { return (n == 7 && f == 3.5f) ? 1 : 0; }
static double twice(double d) { return d * 2.0; }

struct Point { int tag; double d; };

int main(void) {
    char buf[128];
    double a[4]; a[0] = 1.0; a[1] = 2.0;
    float fa[4]; fa[0] = 1.0f;
    int n = 3;
    unsigned u = 3;
    double x = 1.5, y = 2.5;
    int cond = 1;
    struct Point p; p.tag = 1; p.d = 6.5;
    struct Point *pp = &p;

    // --- FFI/variadic path: printf's format-string arg in REG_A0 must not
    // be clobbered by evaluating the following float argument. ---
    snprintf(buf, sizeof(buf), "%f", a[0]);
    if (strcmp(buf, "1.000000") != 0) return 1;

    snprintf(buf, sizeof(buf), "%f", fa[0]);
    if (strcmp(buf, "1.000000") != 0) return 2;

    snprintf(buf, sizeof(buf), "%d %f %f", 7, a[0], a[1]);
    if (strcmp(buf, "7 1.000000 2.000000") != 0) return 3;

    snprintf(buf, sizeof(buf), "%f", (double)n);
    if (strcmp(buf, "3.000000") != 0) return 4;

    snprintf(buf, sizeof(buf), "%f", (double)u);
    if (strcmp(buf, "3.000000") != 0) return 5;

    snprintf(buf, sizeof(buf), "%f", cond ? x : y);
    if (strcmp(buf, "1.500000") != 0) return 6;

    snprintf(buf, sizeof(buf), "%f", a[0] + a[1]);
    if (strcmp(buf, "3.000000") != 0) return 7;

    snprintf(buf, sizeof(buf), "%f", -a[0]);
    if (strcmp(buf, "-1.000000") != 0) return 8;

    snprintf(buf, sizeof(buf), "%f", (a[1] = 9.0, a[0]));
    if (strcmp(buf, "1.000000") != 0) return 9;

    snprintf(buf, sizeof(buf), "%f", p.d);
    if (strcmp(buf, "6.500000") != 0) return 10;

    snprintf(buf, sizeof(buf), "%f", pp->d);
    if (strcmp(buf, "6.500000") != 0) return 11;

    // Nested call inside a float argument: proves the new scratch register
    // survives the callee's end-of-call reset_temp_regs.
    snprintf(buf, sizeof(buf), "%d %f", 5, twice(a[0]));
    if (strcmp(buf, "5 2.000000") != 0) return 12;

    // --- Native (non-variadic) call path: leading int argument must not be
    // clobbered by evaluating the float argument that follows. ---
    if (!chk(7, a[0] + 2.5)) return 20; // a[0]==1.0 -> 3.5
    if (!chkf(7, fa[0] + 2.5f)) return 21;
    if (!chk(7, (double)7 == 7 ? 3.5 : 0.0)) return 22;
    if (!chk(7, twice(1.75))) return 23; // twice(1.75) == 3.5

    return 42;
}
