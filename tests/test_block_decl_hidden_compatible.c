// CCCC_FLAGS: --std=c17
// CCCC_NATIVE_SKIP: the host compiler rejects an unprototyped call with fewer arguments than the definition has
void k(void) {
    int q(int);
}
int q();

int main(void) {
    if (0)
        q(1.5, 2);
    return q(42);
}

int q(int x) {
    return x;
}
