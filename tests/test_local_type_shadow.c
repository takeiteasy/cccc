// Expected return: 42
// Block-scope typedefs and tags may reuse a name visible in an outer scope
// for a different type, including after the outer one has been used.
typedef int T;
struct S {
    int a;
};
int V = 5;

static int nested(void) {
    struct S outer = {1};
    {
        struct S {
            char b;
        } inner = {2};
        {
            struct S {
                long c;
            } innermost = {3};
            return outer.a + inner.b + (int)innermost.c;
        }
    }
}

static int siblings(void) {
    int r = 0;
    {
        struct P {
            int x;
        } p = {2};
        r += p.x;
    }
    {
        struct P {
            int x;
        } p = {3};
        r += p.x;
    }
    return r;
}

int main(void) {
    T        early = 10;
    struct S s0    = {4};
    int      v     = V;
    typedef char T;
    typedef short V;
    struct S {
        char b;
    } s1 = {6};
    T c  = 2;
    V w  = 3;
    // 10 + 4 + 5 + 6 + 2 + 3 + sizeof(char) + 6 + 5 = 42
    return early + s0.a + v + s1.b + c + w + (int)sizeof(T) + nested() +
           siblings();
}
