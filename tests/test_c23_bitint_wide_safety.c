// Test wide _BitInt(N>64) arithmetic under safety instrumentation (#457).
// Regression: --safety=standard/-2/-3 previously tripped the
// "UNINITIALIZED VARIABLE READ" trap on all wide _BitInt locals because the
// ND_VAR scalar instrumentation guard did not exclude wide _BitInt (address-
// based storage), while the write path correctly never emitted MARKI/MARKW.
// CCCC_FLAGS: --std=c23 -2

int main(void) {
    // Basic arithmetic at _BitInt(128) — the ticket's exact repro
    _BitInt(128) x = 5, y = 3;
    if (x + y != 8) return 1;
    if (x - y != 2) return 2;
    if (x * y != 15) return 3;

    // _BitInt(256) to exercise wider storage
    _BitInt(256) a = 1000000000wb, b = 999999999wb;
    if (a + b != 1999999999wb) return 4;
    if (a - b != 1wb) return 5;
    if (a > b ? 0 : 1) return 6;

    // Local assignment then read (the pattern that tripped #457)
    _BitInt(128) r;
    r = x + y;
    if (r != 8) return 7;

    return 42;
}
