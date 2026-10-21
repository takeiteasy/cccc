// A static of a function-local type (typedef enum) round-trips under -c=native.
int main(void) {
    typedef struct L {
        int v;
    } A;
    static A x = {42};
    enum E { K = 7 };
    static enum E e = K;
    return x.v + (int)e - 7;
}
