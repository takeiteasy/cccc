// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A cleanup handler runs at scope exit, so a bound it shrinks is re-read.

int g_len = 4;
int *[[cccc::array, cccc::count(g_len)]] g_data;

void shrink(int *p) {
    g_len = 1;
}

int f(int c) {
    int x = g_data[c];
    {
        int z __attribute__((cleanup(shrink)))  = 0;
        x                                      += z;
    }
    return x + g_data[c];
}

int main(void) {
    int a[4] = {0};
    g_data   = a;
    f(2);
    return 42;
}
