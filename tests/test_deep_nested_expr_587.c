// Regression test for ticket #587: deeply nested / right-leaning binary
// expression trees exhausted the fixed temp-register pool (11 regs, T0-T10).
// Ticket #295 fixed only the left spine; the right spine still grew O(depth)
// live temps. The codegen now spills the LHS to the stack under register
// pressure, bounding peak register use regardless of tree shape.

static int one(void) { return 1; }

int main(void) {
    // Right-nested int addition, depth 40 (well past the old limit of 11).
    // 0 + 1 + 1 + ... (40 ones) = 40
    int ri =
        (1+(1+(1+(1+(1+(1+(1+(1+(1+(1+
        (1+(1+(1+(1+(1+(1+(1+(1+(1+(1+
        (1+(1+(1+(1+(1+(1+(1+(1+(1+(1+
        (1+(1+(1+(1+(1+(1+(1+(1+(1+(1+
        0))))))))))))))))))))))))))))))))))))))));
    if (ri != 40) return 1;

    // Right-nested subtraction with distinct operands — verifies that the
    // spill path preserves operand order for non-commutative ops.
    // 9-(8-(7-(6-(5-(4-(3-(2-(1-0)))))))) = 5
    int rs = (9-(8-(7-(6-(5-(4-(3-(2-(1-0)))))))));
    if (rs != 5) return 2;

    // Deep right-nested chain with a function call as the innermost operand —
    // exercises the spill path together with call-clobbered temp registers.
    // 0 + 1*15 + one() = 16
    int rc =
        (1+(1+(1+(1+(1+(1+(1+(1+(1+(1+
        (1+(1+(1+(1+(1+ one())))))))))))))));
    if (rc != 16) return 3;

    // Deep right-nested multiplication / division / unsigned comparison —
    // exercises the spill path for non-add opcodes (rd aliases the RHS operand).
    // 1*1*...*6 = 6 ; 1/1/.../1 = 1.
    int rm =
        (1*(1*(1*(1*(1*(1*(1*(1*(1*(1*
        (1*(1*(1*(1*(1*(1*(1*(1*(1*(1*6))))))))))))))))))));
    if (rm != 6) return 5;
    int rd2 =
        (1/(1/(1/(1/(1/(1/(1/(1/(1/(1/
        (1/(1/(1/(1/(1/(1/(1/(1/(1/(1/1))))))))))))))))))));
    if (rd2 != 1) return 6;
    // Right-nested unsigned compares collapse to 0 here; checks U-compare
    // opcode selection on the spill path.
    unsigned ru =
        (1u<(1u<(1u<(1u<(1u<(1u<(1u<(1u<(1u<(1u<
        (1u<(1u<(1u<(1u<(1u<(1u<(1u<(1u<(1u<(1u<2u))))))))))))))))))));
    if (ru != 0u) return 7;

    // Right-nested float addition, depth 40 — exercises the float spill path
    // (FR2R/R2FR bit moves around PSH3/POP3).
    float rf =
        (1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+
        (1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+
        (1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+
        (1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+
        0.0f))))))))))))))))))))))))))))))))))))))));
    if ((int)rf != 40) return 4;

    return 42;
}
