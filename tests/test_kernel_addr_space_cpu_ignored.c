// Address-space marks outside kernel code are ignored: the same mismatches
// that [[cccc::kernel]] rejects compile and run.
int use([[cccc::global]] int *g) {
    int                    *p = g;
    [[cccc::local]] int    *q = p;
    [[cccc::constant]] int *c = (int *)q;
    return *c;
}

int main(void) {
    int x = 42;
    return use(&x);
}
