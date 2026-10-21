// A widening cast under a shift round-trips under -c=native.
static long long pack(int stack_size, int spill_param_count) {
    return (long long)stack_size | ((long long)spill_param_count << 32);
}
static int fib(int n) {
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}
int main(void) {
    long long p  = pack(48, 3);
    int       hi = (int)((p >> 32) & 0xFFFFFFFF);
    int       lo = (int)(p & 0xFFFFFFFF);
    if (hi != 3 || lo != 48)
        return 1;
    if (fib(10) != 55)
        return 2;
    return 42;
}
