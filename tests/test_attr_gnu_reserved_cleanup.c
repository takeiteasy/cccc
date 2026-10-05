// [[__gnu__::__cleanup__(fn)]] runs fn at scope exit like [[gnu::cleanup(fn)]].
// CCCC_NATIVE_SKIP: -c=native drops cleanup attributes (#1431)

static int released;
static void release(int *p) {
    released += *p;
}

int main(void) {
    {
        int r [[__gnu__::__cleanup__(release)]] = 42;
        (void)r;
    }
    return released;
}
