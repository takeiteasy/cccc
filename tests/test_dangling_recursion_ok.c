// CCCC_FLAGS: -3
// Ticket #673: a recursive function takes &local and uses it within its own
// activation. Each activation gets a distinct frame epoch (frame identity is
// the runtime bp, same address-keying discipline as #671's CHKL fix), so one
// activation's &local must never be mistaken for another's, and using a
// pointer within the activation that created it must never false-positive.
// Depth 15 is deliberate: it pushes the int-keyed liveness HashMaps (both
// #673's live_epochs/stack_ptr_epochs and the pre-existing CHKL
// stack_var_active) past their initial capacity, forcing at least one
// rehash. This regression-pins the hashmap.c fix alongside #673's own logic
// -- rehash() used to re-insert int-keyed entries via the string hash path,
// corrupting lookups for every int-keyed HashMap in the VM once triggered.
int sum(int n) {
    int acc = 0;
    int *p = &acc;
    if (n <= 0)
        return *p;
    *p = n + sum(n - 1);
    return *p;
}

int main(void) {
    return sum(15) == 120 ? 42 : 1;
}
