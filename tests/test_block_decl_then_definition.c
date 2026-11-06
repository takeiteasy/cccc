// CCCC_FLAGS: --std=c17
int (*in_block(void))(int) {
    int q(int);
    return q;
}

int q(int x) {
    return x;
}

int main(void) {
    if (in_block() != q)
        return 1;
    return in_block()(42);
}
