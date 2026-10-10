// The address of a member of a block-scope compound literal of a struct with a
// _BitInt(N>128) bit-field.
struct WithTag {
    int tag;
    _BitInt(256) f : 100;
    long arr[3];
    struct {
        int p;
        int q;
    };
};

int main(void) {
    int *p = &((struct WithTag){7}).tag;
    if (*p != 7)
        return 1;
    long *a = &((struct WithTag){.arr = {1, 2, 3}}).arr[1];
    if (*a != 2)
        return 2;
    int *q = &((struct WithTag){.q = 5}).q;
    if (*q != 5)
        return 3;
    *q = 6;
    return *q == 6 ? 42 : 4;
}
