// A static table of label addresses round-trips under -c=native.

static int dispatch(int idx) {
    static const void *tab[] = {&&zero, &&ten, &&twenty};
    goto              *tab[idx];
zero:
    return 0;
ten:
    return 10;
twenty:
    return 20;
}

int main(void) {
    return dispatch(0) + dispatch(1) + dispatch(2) + 12;
}
