// #1312: found via #1132's self-hosting spike (stage (c) -- the linked
// self-hosted `cccc`'s VM read every guest function's incoming `int`
// parameter as 0: `id(int a){return a;} id(42)` -> 0, `fib(10)` -> 0).
//
// serialize_expr.c's ND_CAST case suppresses a same-signedness widening
// integer cast as "always implicit in C". That is true almost everywhere
// -- but NOT for a shift operand: `<<`/`>>` apply the integer promotions
// (to `int`) to each operand independently and never the usual arithmetic
// conversions, so `(long long)x << 32` and `x << 32` genuinely differ once
// the cast widens past `int` -- the latter is a 32-bit shift, undefined for
// a count >= 32.
//
// src/codegen_func.c packs `spill_param_count` (an `int`) into the ENT3
// instruction operand's high word exactly this way:
//   ent3_operand = (long long)stack_size | ((long long)spill_param_count << 32);
// With the cast dropped the shift ran in 32 bits, the high word came out
// zero, op_ENT3_fn read `spill_param_count == 0`, and no argument register
// was ever spilled to its parameter slot -- so every parameter read back as
// its zero-initialised slot. No -m shape assertion catches it; only running
// the -c=native output does (this file, under the suite's --native round
// trip, plus tools/comptime_native_smoke.py's
// case_widening_cast_shift_1312).
//
// Fixed by serialize_shift_operand() in serialize_expr.c: a shift operand
// that is a would-be-suppressed widening cast past `int` now keeps its
// explicit cast.

// Mirrors the ENT3 operand pack/unpack: an `int` field shifted into a
// 64-bit word's high half, then read back.
static long long pack(int stack_size, int spill_param_count) {
    return (long long)stack_size | ((long long)spill_param_count << 32);
}

static int hi_word(long long packed) {
    return (int)((packed >> 32) & 0xFFFFFFFF);
}

static int lo_word(long long packed) {
    return (int)(packed & 0xFFFFFFFF);
}

// The behaviour the miscompile actually broke: a callee reading its own
// incoming parameter. Recursion makes a dropped parameter fatal (infinite
// or immediate wrong answer) rather than merely wrong.
static int fib(int n) {
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

static int add3(int a, int b, int c) {
    return a + b + c;
}

int main(void) {
    long long p = pack(48, 3);
    if (hi_word(p) != 3 || lo_word(p) != 48)
        return 1;

    // right shift of a widened negative value: 64-bit arithmetic shift, not
    // 32-bit.
    long long q = (long long)-1 >> 1;
    if (q != -1)
        return 2;

    if (fib(10) != 55)
        return 3;
    if (add3(10, 20, 12) != 42)
        return 4;

    return 42;
}
