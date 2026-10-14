// CCCC_FLAGS: -3
// Taking the same address again under provenance tracking replaces its entry.
int main(void) {
    int  x   = 0;
    int *p[2] = {0, 0};
    for (int i = 0; i < 100000; i++) {
        p[i & 1] = &x;
        *p[i & 1] += 1;
    }
    return x == 100000 ? 42 : 1;
}
