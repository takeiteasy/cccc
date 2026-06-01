int choose(int x) {
    if (x)
        return 5;
    return 9;
}

int main(void) {
    if ((21 + 21) != 42)
        return 4;
    if ((50 - 8) != 42)
        return 5;
    if ((6 * 7) != 42)
        return 6;
    if ((42 + 0) != 42)
        return 7;
    if ((42 * 1) != 42)
        return 8;

    int a = 40;
    int b = 2;
    int c = a + b;
    int d = choose(0);

    if (c != 42)
        return 1;
    if (d != 9)
        return 2;
    if ((7 / (d - 9 + 1)) != 7)
        return 3;
    return 42;
}
