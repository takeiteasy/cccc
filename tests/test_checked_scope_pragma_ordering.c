// Checked regions (#485): a #pragma cccc checked begin appearing AFTER an
// unchecked declaration must not retroactively reject it -- the token-
// stamping design (mirroring #pragma pack(N)) gets this right by
// construction, since only tokens produced after the pragma is processed
// are stamped. Contrast with #pragma cccc config(checked_pointers = true),
// which resolves for the whole file before parsing begins and therefore
// behaves the opposite way (see
// tests/test_checked_pointers_prop_pragma_after.c).

int *p_before_the_pragma = 0; // declared before any region is open -- fine

#pragma cccc checked begin

int *[[cccc::single]] q_inside_the_region = 0;

#pragma cccc checked end

int main(void) {
    (void)p_before_the_pragma;
    (void)q_inside_the_region;
    return 42;
}
