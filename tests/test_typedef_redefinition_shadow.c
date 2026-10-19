// A nested scope may give a typedef name a different type.
typedef int T;

int main(void) {
    typedef char T;
    T            c = 40;
    {
        typedef long T;
        T            l = 2;
        return c + (int)l;
    }
}
