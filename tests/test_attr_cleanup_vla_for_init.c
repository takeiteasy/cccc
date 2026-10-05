// A cleanup VLA declared in a for-loop initializer is cleaned up when the loop
// ends, not at the end of each iteration.

static int calls;
static void rel(int (*p)[]) {
    (void)p;
    calls++;
}

int main(void) {
    int k = 3;
    for (int v[k] __attribute__((cleanup(rel))), i = 0; i < 2; i++) {
        v[i] = i;
        if (calls != 0)
            return 1;
    }
    return calls == 1 ? 42 : 2;
}
