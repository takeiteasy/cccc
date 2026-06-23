// CCCC_FLAGS: -ffuse

int main(void) {
    long x = 7;
    long y = x * 6;
    long z = y + 0;
    return (int)z;
}
