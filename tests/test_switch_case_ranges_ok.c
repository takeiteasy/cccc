// #815 (negative test): adjacent-but-non-overlapping case ranges and
// distinct scalar case values must still compile and run normally.
int main(void) {
    int r = 0;
    switch (7) {
        case 1 ... 5:
            r = 1;
            break;
        case 6 ... 9:
            r = 2;
            break;
        default:
            r = 3;
            break;
    }
    if (r != 2)
        return 1;

    switch (10) {
        case 10:
            r = 10;
            break;
        case 11:
            r = 11;
            break;
        default:
            r = 0;
            break;
    }
    if (r != 10)
        return 1;

    return 42;
}
