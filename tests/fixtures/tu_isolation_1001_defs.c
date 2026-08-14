// Fixture TU1 for tests/test_tu_macro_isolation_1001.c and
// tests/test_tu_guard_isolation_1001.c (#1001).
#define TU_ISOLATION_1001_MACRO 20
int tu_isolation_1001_use_a(void) { return TU_ISOLATION_1001_MACRO; }
