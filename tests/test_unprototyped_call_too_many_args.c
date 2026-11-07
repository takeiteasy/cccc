// CCCC_FLAGS: --std=c17
int q();

int main(void) {
    if (0)
        q(1.5, 2);
    return q(42);
}
int q(int x) {
    return x;
}
