// A cleanup attribute on an `auto` declarator is claimed like on any other
// automatic variable, wherever the attribute is written.

static int hits;
static void bump(int *p) {
    hits += *p;
}

int main(void) {
    {
        auto a __attribute__((cleanup(bump))) = 1;
        auto b [[gnu::cleanup(bump)]]         = 10;
        __attribute__((cleanup(bump))) auto c = 100;
        (void)a;
        (void)b;
        (void)c;
    }
    if (hits != 111)
        return 1;

    // A copy of a cleanup variable does not inherit its cleanup.
    hits = 0;
    {
        int  v __attribute__((cleanup(bump))) = 5;
        auto copy                             = v;
        (void)copy;
    }
    if (hits != 5)
        return 2;

    return 42;
}
