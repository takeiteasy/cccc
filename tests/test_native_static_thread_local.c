// A `static thread_local` global is declared once under -c=native: gcc rejects
// a forward declaration followed by the definition as a redefinition.
static thread_local int initialized = 5;
static thread_local int zeroed;

static int bump(void) {
    return ++initialized + ++zeroed;
}

int main(void) {
    int *p = &(thread_local int){13};
    if (*p != 13)
        return 1;
    if (bump() != 7)
        return 2;
    return initialized == 6 && zeroed == 1 ? 42 : 3;
}
