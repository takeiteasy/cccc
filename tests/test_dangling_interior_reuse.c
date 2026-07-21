// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --dangling-pointers
// Ticket #675: retention correctness for reused stack addresses. Unlike
// sorted_allocs' bump-allocated heap bases, stack addresses ARE reused --
// `holder`'s frame is sized (via `pad`) to reoccupy part of the same memory
// `get_local`'s `arr` used. arr's retained interval must NOT be evicted or
// overwritten just because that address range is later reoccupied for an
// unrelated, non-escaping purpose (`pad` never escapes, so it gets no
// STKTAG entry of its own) -- dereferencing the interior pointer into arr's
// (now-dead) extent must still be flagged.
int *get_local(int i) {
    int arr[8];
    arr[i] = 99;
    return &arr[i];
}

int holder(int *p) {
    int pad[6]; // reoccupy part of arr's former memory with a live, smaller object
    pad[0] = 0;
    return *p; // p points past pad's extent, into arr's now-dead remainder
}

int main(void) {
    int *p = get_local(7); // arr's last element -- outside pad's narrower extent
    return holder(p);
}
