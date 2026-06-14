// CCCC_FLAGS: --optimize=4

long seed = 5;

int main(void) {
    long base = 12;
    long index = seed;
    long result = base + index * 6;
    return (int)result;
}
