// CCCC_FLAGS: --std=c17
int z();

int main(void) {
    if (0)
        z();
    return z(42);
}
int z(int x) {
    return x;
}
