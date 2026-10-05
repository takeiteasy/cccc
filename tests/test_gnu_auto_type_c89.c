// CCCC_FLAGS: --std=c89
int main(void) {
    int         a = 41;
    __auto_type t = a;
    return t + 1;
}
