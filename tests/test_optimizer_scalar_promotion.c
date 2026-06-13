// CCCC_FLAGS: --optimize=3

int bump(int x) {
    int y = x;
    for (int i = 0; i < 5; i++)
        y += i;
    return y;
}

int main(void) {
    int sum = 0;
    int i = 0;
    for (; i < 12; i++)
        sum += i;

    int mixed = 1;
    for (int j = 0; j < 4; j++) {
        mixed += bump(j);
        if (mixed & 1)
            mixed += j;
        else
            mixed -= j;
    }

    if (sum != 66)
        return 1;
    if (i != 12)
        return 2;
    if (mixed != 45)
        return 3;
    return 42;
}
