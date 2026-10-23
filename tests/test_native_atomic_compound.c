// _Atomic compound assignment and ++/-- agree between the VM and -c=native.
struct S {
    _Atomic int c;
};
int main(void) {
    struct S     s  = {0};
    _Atomic int  x  = 5;
    _Atomic char ch = 120;
    int          arr[4];
    int *_Atomic p  = arr;
    s.c            += 2;
    x              -= 1;
    x              &= 6;
    x              |= 8;
    x              ^= 1;
    ch             += 10;
    p              += 2;
    p++;
    x       *= 3;
    int old  = x++;
    int nw   = ++x;
    x       += 1.5;
    return s.c == 2 && x == 42 && ch == -126 && p == arr + 3 && old == 39 &&
                   nw == 41
               ? 42
               : 1;
}
