// -c=native, __builtin_{add,sub,mul}_overflow round-trip, overflowing and not
int main(void) {
    int r;
    int ok = __builtin_add_overflow(2, 3, &r);
    if (ok || r != 5)
        return 1;
    ok = __builtin_sub_overflow(10, 3, &r);
    if (ok || r != 7)
        return 2;
    ok = __builtin_mul_overflow(6, 7, &r);
    if (ok || r != 42)
        return 3;
    ok = __builtin_add_overflow(2147483647, 1, &r);
    if (!ok)
        return 4;
    ok = __builtin_mul_overflow(1000000000, 10, &r);
    if (!ok)
        return 5;
    return 42;
}
