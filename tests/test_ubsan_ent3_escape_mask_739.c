// Ticket #739: patching ENT3's frame-epoch masks for a function with an
// escaping local used to compute the packed mask in a *signed* long long,
// which triggered UBSan's signed-left-shift-into-sign-bit check on every
// function taking the address of a local scalar/array. Fixed by packing the
// masks in an unsigned long long instead. This is the ticket's minimal
// repro -- it produces the correct result on a plain build, and must not
// print a UBSan diagnostic when built/run with `make ubsan`.
void takes_ptr(int *p) {
    *p = 1;
}

int main(void) {
    int local_scalar = 0;
    takes_ptr(&local_scalar);
    return local_scalar == 1 ? 42 : 1;
}
