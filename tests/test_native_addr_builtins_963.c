// -c=native, frame/return address and the trap builtins compile and run
int trapper(int x) {
    if (x > 0)
        return 42;
    __builtin_unreachable();
}
int main(void) {
    void *fp = __builtin_frame_address(0);
    void *ra = __builtin_return_address(0);
    (void)ra;
    return fp ? trapper(1) : 1;
}
