// CCCC_FLAGS: --optimize=3

int main(void) {
    int ints[8];
    char bytes[8];
    double doubles[4];

    for (int i = 0; i < 8; i++) {
        ints[i] = i * 3 + 1;
        bytes[i] = (char)(i + 2);
    }
    for (int i = 0; i < 4; i++)
        doubles[i] = (double)(i + 1) * 1.5;

    int sum = 0;
    for (int i = 0; i < 8; i++) {
        sum = sum + ints[i];
        sum = sum + bytes[i];
    }

    int scaled = 0;
    for (int i = 0; i < 4; i++)
        scaled = scaled + (int)(doubles[i] * 2.0);

    if (sum != 136)
        return 1;
    if (scaled != 30)
        return 2;
    return 42;
}
