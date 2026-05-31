int callee(int x) {
    return x + 1;
}

int (*global_fp)(int) = callee;

int main(void) {
    long long wide = 0x100000000LL + 40;
    if ((int)(wide - 0x100000000LL) != 40)
        return 1;

    int (*local_fp)(int) = callee;
    if (local_fp(41) != 42)
        return 2;
    if (global_fp(41) != 42)
        return 3;

    goto *&&done;
    return 4;

done:
    return 42;
}
