// -c=native, `&v` on a VLA local yields the array's data address (not the frame
// slot holding the alloca'd pointer) and keeps its non-decayed `int (*)[n]`
// row-stride type
int main(void) {
    int n = 3;
    int v[n];
    v[0]        = 7;
    v[1]        = 8;
    v[2]        = 9;
    int (*p)[n] = &v;
    (*p)[1]     = 42;
    int  total  = v[1]; // must see the write through p

    long stride = (long)((char *)(&v + 1) - (char *)&v);
    total       = total - total + (stride == (long)(n * sizeof(int)) ? 42 : 1);

    return total;
}
