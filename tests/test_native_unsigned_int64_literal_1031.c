// -c=native, a folded ND_NUM integer literal now serializes with a
// sign/width-accurate suffix instead of a bare `%lld` of the raw bit pattern --
// an unsigned 64-bit value >= 2^63 (e.g. 18446744073709551615ULL, ULLONG_MAX)
// used to print as the unsuffixed text `-1`, which a real host compiler reads
// back as a negative `int`; a folded INT64_MIN used to print as the bare token
// `-9223372036854775808`, not a valid signed literal at all ('integer literal
// is too large...'). Asserts VM 42 -> native 42

double g_double_from_u64 = (double)18446744073709551615ULL;

int main(void) {
    if (g_double_from_u64 != 18446744073709551616.0)
        return 1;

    volatile unsigned long long umax = 18446744073709551615ULL;
    double                      d    = (double)umax;
    if (d != 18446744073709551616.0)
        return 2;
    if (d < 0)
        return 3;

    volatile long long neg = -9223372036854775807LL - 1;
    if (neg >= 0)
        return 4;
    if ((double)neg != -9223372036854775808.0)
        return 5;

    return 42;
}
