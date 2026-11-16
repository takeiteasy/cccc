// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: _Z1fi\(
// CCCC_EXPECT_STDOUT: _Z1fj\(
// CCCC_EXPECT_STDOUT: _Z1fl\(
// CCCC_EXPECT_STDOUT: _Z1fm\(
// CCCC_EXPECT_STDOUT: _Z1fx\(
// CCCC_EXPECT_STDOUT: _Z1fy\(
// CCCC_EXPECT_STDOUT: _Z1fs\(
// CCCC_EXPECT_STDOUT: _Z1ft\(
// CCCC_EXPECT_STDOUT: _Z1fh\(
// CCCC_EXPECT_STDOUT: _Z1fb\(
// CCCC_EXPECT_STDOUT: _Z1ff\(
// CCCC_EXPECT_STDOUT: _Z1fd\(
// CCCC_EXPECT_STDOUT: _Z1fe\(
// CCCC_EXPECT_STDOUT: _Z1fPiS_\(
// CCCC_EXPECT_STDOUT: _Z1fPKcS0_\(
// CCCC_EXPECT_STDOUT: _Z1fPcPKc\(
// CCCC_EXPECT_STDOUT: _Z1fPKiPi\(
// CCCC_EXPECT_STDOUT: _Z1fPVi\(
// CCCC_EXPECT_STDOUT: _Z1fPVKi\(
// CCCC_EXPECT_STDOUT: _Z1f1S\(
// CCCC_EXPECT_STDOUT: _Z1fP1SS0_\(
// CCCC_EXPECT_STDOUT: _Z1fPK1SPS_P1U\(
// CCCC_EXPECT_STDOUT: _Z1f1E\(
// CCCC_EXPECT_STDOUT: _Z1fPFiiE\(
// CCCC_EXPECT_STDOUT: _Z1fPFiidES0_\(
// CCCC_EXPECT_STDOUT: _Z1fiz\(
// CCCC_EXPECT_STDOUT: _Z1fz\(
// CCCC_EXPECT_STDOUT: _Z1fCf\(
// CCCC_EXPECT_STDOUT: _Z1fCd\(
// CCCC_EXPECT_STDOUT: _Z1fPA3_i\(
// CCCC_EXPECT_STDOUT: _Z1fPPiS0_S_\(
// CCCC_EXPECT_STDOUT: _Z1fPvPKv\(
// Itanium names, as clang writes them for `__attribute__((overloadable))`.
#define O __attribute__((overloadable))
struct S {
    int x;
};
union U {
    int   a;
    float b;
};
enum E { E0, E1 };
typedef int (*Fp)(int, double);

void O f(void) {}
void O f(int a) {}
void O f(unsigned a) {}
void O f(long a) {}
void O f(unsigned long a) {}
void O f(long long a) {}
void O f(unsigned long long a) {}
void O f(short a) {}
void O f(unsigned short a) {}
void O f(unsigned char a) {}
void O f(_Bool a) {}
void O f(float a) {}
void O f(double a) {}
void O f(long double a) {}
void O f(int *a, int *b) {}
void O f(const char *a, const char *b) {}
void O f(char *a, const char *b) {}
void O f(const int *a, int *const b) {}
void O f(volatile int *a) {}
void O f(const volatile int *a) {}
void O f(struct S s) {}
void O f(struct S *s, struct S *t) {}
void O f(const struct S *s, struct S *t, union U *u) {}
void O f(enum E e) {}
void O f(int (*cb)(int)) {}
void O f(Fp cb, Fp cb2) {}
void O f(int a, ...) {}
void O f(...) {}
void O f(float _Complex c) {}
void O f(double _Complex c) {}
void O f(int (*a)[3]) {}
void O f(int **a, int **b, int *c) {}
void O f(void *p, const void *q) {}

int main(void) {
    return 42;
}
