// CCCC_FLAGS: -3
// Ticket #675: an interior pointer into a still-live frame must not be
// flagged. STKTAG retains arr's extent tagged with main's own epoch, which
// stays in vm->live_epochs for as long as main is on the call stack, so
// CHKP3's interior interval-stabbing lookup must resolve this as live.
int *interior(int *arr, int i) {
    return &arr[i];
}

int main(void) {
    int arr[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    int *p = interior(arr, 5);
    return (*p == 5) ? 42 : 1;
}
