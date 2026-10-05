// A VLA declared in a for-loop initializer round-trips through -c=native: the
// init is emitted in order inside a block that scopes the VLA to the loop, so
// a length may use an earlier declarator.

int main(void) {
    int k = 2, s = 0;
    for (int n = 3, v[n], i = 0; i < n; i++) {
        v[i]  = i + 1;
        s    += v[i];
    }
    for (int u[k]; k < 4; k++) {
        u[0]  = 1;
        s    += u[0];
        if (k == 3)
            break;
    }
    return s == 8 ? 42 : 1;
}
