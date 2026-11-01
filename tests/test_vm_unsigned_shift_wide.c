// A 64-bit source can be negative, so narrowing its unsigned shift keeps the
// extension.
[[gnu::noinline]] unsigned wide_shr(long *p) {
    return (unsigned)((unsigned long)*p >> 4);
}

int main(void) {
    long neg = -1;
    return wide_shr(&neg) == 0xFFFFFFFFu ? 42 : 1;
}
