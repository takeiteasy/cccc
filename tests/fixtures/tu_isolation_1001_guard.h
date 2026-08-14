// Guarded header shared by tests/fixtures/tu_isolation_1001_guard_a.c and
// tests/test_tu_guard_isolation_1001.c (#1001). Declares only (no
// non-extern global definition) so two TUs each getting their own complete
// copy is unconditionally correct, unlike a header that *defines* a
// non-extern global (see man/COVERAGE.md's #1001 writeup for why that
// shape is a real ODR violation, not a bug in this fix).
#ifndef TU_ISOLATION_1001_GUARD_H
#define TU_ISOLATION_1001_GUARD_H
typedef unsigned long TuIsolation1001Value;
static inline TuIsolation1001Value tu_isolation_1001_box(unsigned long x) { return x; }
#endif
