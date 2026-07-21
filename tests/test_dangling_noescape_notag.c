// CCCC_FLAGS: -3
// Ticket #676: LEA3 no longer records into vm->stack_ptr_epochs for a local
// whose address is proven (via the post-parse mark_addr_escapes pass) never
// to escape its creating frame -- it isn't passed to a call, returned, or
// stored into a pointer/aggregate lvalue. This must not change behavior:
// the pointer is dereferenced only within its own still-live frame, so it
// should run to completion exactly as it did when every &local was recorded
// unconditionally (#673).
static int accumulate(int n) {
    int total = 0;
    for (int i = 0; i < n; i++) {
        int *p = &total; // address taken, but never escapes this call
        *p += i;
    }
    return total;
}

int main(void) {
    int arr[4] = {1, 2, 3, 4};
    int sum = 0;
    for (int i = 0; i < 4; i++) {
        int *elem = &arr[i]; // constant-offset &local, still never escapes
        sum += *elem;
    }
    int acc = accumulate(5); // 0+1+2+3+4 = 10
    return (sum == 10 && acc == 10) ? 42 : 1;
}
