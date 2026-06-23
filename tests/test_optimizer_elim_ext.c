// CCCC_FLAGS: -felim-ext
//
// Test redundant sign/zero-extension elimination.
// The pass removes SX*/ZX* ops whose source register is already provably in
// the target range.  All these cases must still compute the correct value.

#include <stdint.h>

// ---- helpers ----------------------------------------------------------------

static int failures = 0;

#define EXPECT(expr, expected) do {          \
    long long got = (long long)(expr);       \
    long long want = (long long)(expected);  \
    if (got != want) failures++;             \
} while (0)

// ---- 1. Non-adjacent load + extend -----------------------------------------
// A signed 32-bit load (LDR_W) sets SX4 state; an SX4 a few instructions
// later on the same register should be eliminated.
static long long non_adjacent_sx4(int *p) {
    int v = *p;         // LDR_W rd → SX4 state on rd
    long long a = v;    // possible MOV / sign-extend context
    long long b = a + 1;
    // Force the compiler to issue SX4 by casting back through int:
    long long c = (int)b; // SX4 — but b came from SX4 source so redundant
    return c;
}

// ---- 2. Chained extensions --------------------------------------------------
// SX4 of a value already proven SX4 is a no-op.
static long long chained_sx4(long long x) {
    int a = (int)x;           // SX4 → a has EXT_SX4
    int b = a;                // copy
    long long c = (int)b;     // SX4 of SX4 value — redundant
    return c;
}

// ---- 3. ZX1 then ZX2 -------------------------------------------------------
// ZX2 on a ZX1 value is redundant (ZX1 range [0,255] ⊂ ZX2 range [0,65535]).
static long long zx1_then_zx2(unsigned char v) {
    // v already zero-extended to 64 bits as unsigned char
    unsigned short s = v; // ZX2 on ZX1 source — redundant
    return (long long)s;
}

// ---- 4. ZX1 followed by SX2 must NOT be eliminated -------------------------
// A negative ZX1 value (>127) cast to short via SX2 gives a different result.
// This tests that the pass is conservative: SX2 is NOT in the redundancy set
// for ZX1 (ZX1=[0,255]; SX2 would leave 128-255 unchanged since they are
// positive in 16-bit, so actually... let me use a cast path that forces ZX1).
// Actually ZX1=[0,255] ⊂ [0,32767] which IS in the SX2-safe range since all
// ZX1 values are < 2^15, so SX2 IS redundant here per the pass rules.
// We verify the *value* is still correct.
static long long zx1_then_sx2(unsigned char v) {
    short s = (short)v;  // ZX1 is in [0,255] ⊂ SX2 safe range
    return (long long)s;
}

// ---- 5. ZX4 after SX4 must NOT be eliminated --------------------------------
// SX4 can produce negative values; ZX4 on those differs (upper bits cleared).
static long long zx4_after_sx4(int v) {
    // v is SX4 (LDR_W or cast from int); ZX4 is NOT redundant here.
    unsigned int u = (unsigned int)v; // ZX4 — keeps lower 32 bits as unsigned
    return (long long)u;
}

// ---- 6. Branch: state resets at join point ----------------------------------
// After a branch, the pass must conservatively clear range state; the
// extension must still fire correctly.
static long long branch_reset(int cond, int a, int b) {
    int v;
    if (cond)
        v = a;
    else
        v = b;
    // At the join point, ext state is cleared.  The SX4 here must be kept.
    long long r = (int)v;
    return r;
}

// ---- 7. LDR_LOCAL + ZX4 (unsigned int local) --------------------------------
// An unsigned int local forces ZX4 after LDR_LOCAL_W.
// A second ZX4 cast should be eliminated.
static long long local_zx4_chain(unsigned int u) {
    // u arrives sign-extended on the stack; codegen adds ZX4 on first use.
    unsigned int a = u;           // ZX4 → EXT_ZX4
    unsigned long long b = a;     // ZX4 again — redundant
    return (long long)b;
}

// ---- 8. SX2 redundant on ZX1 source -----------------------------------------
// ZX1=[0,255], SX2 range=[-32768,32767]. Since ZX1⊂SX2-safe, SX2 is
// redundant on a ZX1 value.
static long long sx2_on_zx1(unsigned char c) {
    short s = (short)c; // SX2 on ZX1 value — redundant
    return (long long)s;
}

// ---- 9. SX4 redundant on ZX2 source -----------------------------------------
// ZX2=[0,65535] ⊂ [0,2^31-1], so SX4 is redundant on a ZX2 value.
static long long sx4_on_zx2(unsigned short h) {
    int i = (int)h;  // SX4 on ZX2 value — redundant
    return (long long)i;
}

// =============================================================================

int main(void) {
    int base = 7;
    EXPECT(non_adjacent_sx4(&base), 8);

    EXPECT(chained_sx4(-5LL), -5);
    EXPECT(chained_sx4(2147483647LL), 2147483647);
    EXPECT(chained_sx4(-2147483648LL), -2147483648);

    EXPECT(zx1_then_zx2(200), 200);
    EXPECT(zx1_then_zx2(0),   0);
    EXPECT(zx1_then_zx2(255), 255);

    EXPECT(zx1_then_sx2(200), 200);  // 200 is positive in short
    EXPECT(zx1_then_sx2(0),   0);

    EXPECT(zx4_after_sx4(-1),  (long long)(unsigned int)-1);   // 4294967295
    EXPECT(zx4_after_sx4(1),   1);
    EXPECT(zx4_after_sx4(-2147483648), (long long)2147483648ULL);

    EXPECT(branch_reset(1, 42, 0),   42);
    EXPECT(branch_reset(0, 0, -99), -99);

    EXPECT(local_zx4_chain(0u),          0);
    EXPECT(local_zx4_chain(4294967295u), 4294967295LL);

    EXPECT(sx2_on_zx1(127), 127);
    EXPECT(sx2_on_zx1(200), 200);

    EXPECT(sx4_on_zx2(65535), 65535);
    EXPECT(sx4_on_zx2(0),     0);

    if (failures)
        return 1;
    return 42;
}
