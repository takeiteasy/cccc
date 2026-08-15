// #1015: a second, independent tagless typedef'd enum reusing AA1015TD.
typedef enum { AA1015TD = 5, CC1015TD } T1015B;
int b_use_1015td(void) {
    T1015B t = AA1015TD;
    return t + CC1015TD;
}
