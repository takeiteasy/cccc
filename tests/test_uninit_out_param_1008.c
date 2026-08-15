// CCCC_FLAGS: --safety=max
// CCCC_REJECT_STDOUT: UNINITIALIZED VARIABLE READ
// The ordinary C out-parameter idiom (a callee writing a result through a
// pointer obtained from &var, rather than the caller assigning it directly)
// must not trip the uninitialized-variable-read detector (#1008). MARKI is
// only ever emitted for a syntactic `var = expr;`; a write through `&var`
// bypasses it entirely, so the later read of `below`/`found` falsely traps
// unless the read-side CHKI is exempted once a local's address has been
// taken (Obj.addr_taken, set by mark_addr_escapes's new ND_ADDR case).
static void fill(int *out, int *found) {
    *out = 42;
    *found = 1;
}

int main(void) {
    int below;
    int found;
    fill(&below, &found);
    return found ? below : 1;
}
