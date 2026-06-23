// CCCC_FLAGS: -2
// Ticket #587 companion: exercise the LHS-spill binary-op path under checked
// arithmetic (-2). The spill path emits ops as `op rd, r_lhs, rd` where rd
// aliases the RHS operand; under -2 the add/sub/mul opcodes become the checked
// ADDC/SUBC/MULC variants, which the default-level test does not cover.

static int one(void) { return 1; }

int main(void) {
    // Deep right-nested signed add/sub → checked ADDC/SUBC on the spill path.
    int ra =
        (1+(1+(1+(1+(1+(1+(1+(1+(1+(1+
        (1+(1+(1+(1+(1+(1+(1+(1+(1+(1+
        0))))))))))))))))))));
    if (ra != 20) return 1;

    int rs = (9-(8-(7-(6-(5-(4-(3-(2-(1-0)))))))));
    if (rs != 5) return 2;

    // Deep nested with a call operand under -2 (checked arithmetic active).
    int rc =
        (1+(1+(1+(1+(1+(1+(1+(1+(1+(1+
        (1+(1+(1+(1+(1+ one())))))))))))))));
    if (rc != 16) return 3;

    // Deep right-nested multiplication → checked MULC on the spill path.
    int rm =
        (1*(1*(1*(1*(1*(1*(1*(1*(1*(1*
        (1*(1*(1*(1*(1*(1*(1*(1*(1*(1*7))))))))))))))))))));
    if (rm != 7) return 4;

    return 42;
}
