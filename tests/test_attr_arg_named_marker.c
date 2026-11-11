// An attribute argument spelled like a cccc marker is an argument, not a
// marker: the declaration it belongs to stays.

static int ran;
static void comptime(int *p) {
    ran += *p;
}
static void test(int *p) {
    ran += *p * 10;
}

int main(void) {
    {
        [[gnu::cleanup(comptime)]] int a = 1;
        [[gnu::cleanup(test)]] int    b = 2;
        (void)a;
        (void)b;
    }
    return ran == 21 ? 42 : 1;
}
