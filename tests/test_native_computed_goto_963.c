// -c=native, labels-as-values and `goto *ptr` round-trip
int main(void) {
    void *tab[2];
    tab[0]    = &&one;
    tab[1]    = &&two;
    int   acc = 0;
    int   i   = 0;
    goto *tab[i];
one:
    acc += 20;
    goto *tab[1];
two:
    acc += 22;
    return acc;
}
