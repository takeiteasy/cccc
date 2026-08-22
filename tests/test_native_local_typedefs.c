// -c=native regressions (#1116, #1117).
//
// #1116: a function-local typedef of an ANONYMOUS aggregate serialized
// neither its struct body nor its alias when a structurally identical
// anonymous typedef existed in a sibling function (the global collection
// pre-pass runs with no current function, so the nominal-distinctness check
// could not see either side's alias and merged the two Types into one
// ctx->defs slot owned by whichever function was collected first). The
// losing function then referenced an undeclared alias. Same asymmetry also
// merged a file-scope tagged struct with an identical same-tag function-
// local one, leaving every other function's use incomplete.
//
// #1117: spelled complex accessors (creal/cimag/conj, CMPLX/I) that survive
// into the generated text are expanded by the HOST through the replayed
// bundled complex.h/tgmath.h macros, which reference cccc-internal
// __cccc_creal*/__cccc_cimag*/__cccc_conj*/__cccc_cmplx* names; those now
// get static inline host definitions whenever complex machinery is
// reachable.
#include <complex.h>
#include <stdio.h>

struct Shadowed {
    int a;
    int b;
};

typedef struct {
    int width;
    int height;
} LocalShapeA;

static int sibling_b(void) {
    // Structurally identical to LocalShapeA but nominally its own type:
    // before #1116 this merge dropped BOTH the body and the alias here.
    typedef struct {
        int width;
        int height;
    } LocalShapeB;
    LocalShapeB b;
    b.width  = 20;
    b.height = 22;
    return b.width + b.height;
}

static int shadow_reader(void) {
    // Uses the file-scope struct Shadowed while another function declares
    // its own identical-shape local tag of the same name.
    struct Shadowed s;
    s.a = 30;
    s.b = 12;
    return s.a + s.b;
}

static int local_shadow(void) {
    // Function-local `struct Shadowed` shadows the file-scope one; both
    // definitions must be emitted (file-scope at top level, this one
    // locally) for both functions to compile and keep their own layout.
    struct Shadowed {
        int a;
        int b;
    };
    struct Shadowed s;
    s.a = 5;
    s.b = 5;
    return s.a + s.b;
}

int main(void) {
    // #1116 mode 1: anonymous-aggregate typedefs in sibling functions.
    typedef struct {
        int width;
        int height;
    } LocalShapeMain;
    LocalShapeMain m;
    m.width  = 10;
    m.height = 10;
    if (m.width + m.height != 20)
        return 1;
    if (sibling_b() != 42)
        return 2;

    // #1116 mode 2: file-scope tag vs same-shape function-local shadow.
    if (local_shadow() != 10)
        return 3;
    if (shadow_reader() != 42)
        return 4;

    // #1117: spelled accessors and constructors must resolve on the host.
    double _Complex z       = CMPLX(3.0, 4.0);
    float _Complex zf       = CMPLXF(1.0f, 1.0f);
    long double _Complex zl = CMPLXL(2.0L, 1.0L);
    if (creal(z) != 3.0 || cimag(z) != 4.0)
        return 5;
    if (conj(z) != CMPLX(3.0, -4.0))
        return 6;
    if (crealf(zf) != 1.0f || cimagf(zf) != 1.0f)
        return 7;
    if (creall(zl) != 2.0L || cimagl(zl) != 1.0L)
        return 8;
    if (cabs(CMPLX(3.0, 4.0)) != 5.0)
        return 9;
    if (carg(CMPLX(1.0, 1.0)) - 0.7853981633974483 > 1e-12)
        return 10;
    double _Complex eye = 2.0 * I;
    if (creal(eye) != 0.0 || cimag(eye) != 2.0)
        return 11;

    printf("native local typedefs ok\n");
    return 42;
}
