// CCCC_FLAGS: --optimize=3

int plus7(int x) {
    return x + 7;
}

int (*global_fp)(int) = plus7;

int label_value(int x) {
    void *labels[] = {&&zero, &&one, &&two};
    goto *labels[x];
zero:
    return 10;
one:
    return 20;
two:
    return 30;
}

int dense_switch(int x) {
    switch (x) {
        case 0 ... 63:
            return x + 1;
        default:
            return -1;
    }
}

int main(void) {
    int folded = ((10 + 20) * (3 - 1)) ^ (7 & 3);
    if (folded != 63)
        return 1;

    if (global_fp(35) != 42)
        return 2;

    if (label_value(2) != 30)
        return 3;

    if (dense_switch(41) != 42)
        return 4;
    if (dense_switch(64) != -1)
        return 5;

    return 42;
}
