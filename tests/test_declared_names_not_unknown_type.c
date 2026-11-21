// Declaration-shaped statements and casts over declared names stay valid.

typedef int Foo;

int main(void) {
    int  a = 6, b = 7;
    a   *b;
    Foo *p = &a;
    int  c = (Foo)b;
    for (Foo i = 0; i < 1; i++)
        c += i;
    return *p * c;
}
