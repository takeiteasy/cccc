// CCCC_FLAGS: --stack-instrumentation --optimize=3
// CCCC_MATRIX_SKIP: depends on --optimize=3 (copy-prop specifically)
// Ticket #759: SCOPEIN, SCOPEOUT, CHKL, MARKR, MARKW, and STKTAG were
// missing from copy-prop's op_operand_word_is_immediate() classifier.
// SCOPEIN/SCOPEOUT carry a bare scope-id immediate in word 0; CHKL/MARKR/
// MARKW/CHKI/MARKI carry the low 32 bits of an i64 bp-relative offset there.
// Byte 0 of either aliases a real register number whenever the scope id or
// the (positive) offset is small (< NUM_REGS == 32) -- nested block scopes
// push scope ids through 1..31, and stack-passed parameters (9th argument
// onward) sit at small positive bp offsets. Sub-pass B's generic decode
// would misread that byte as a destination and NOP a still-live MOV3
// feeding a value across the scope/offset boundary.
//
// Deeply nested scopes to push scope ids up through the 1..31 aliasing
// window, with a live value threaded through every level so a spurious
// dead-MOV3 elimination shows up as a wrong sum.
static int nested_scopes(int seed) {
    int total = 0;
    {
        int a1  = seed + 1;
        total  += a1;
        {
            int a2  = seed + 2;
            total  += a2;
            {
                int a3  = seed + 3;
                total  += a3;
                {
                    int a4  = seed + 4;
                    total  += a4;
                    {
                        int a5  = seed + 5;
                        total  += a5;
                        {
                            int a6  = seed + 6;
                            total  += a6;
                            {
                                int a7  = seed + 7;
                                total  += a7;
                                {
                                    int a8  = seed + 8;
                                    total  += a8;
                                    {
                                        int a9  = seed + 9;
                                        total  += a9;
                                        {
                                            int a10  = seed + 10;
                                            total   += a10;
                                            {
                                                int a11  = seed + 11;
                                                total   += a11;
                                                {
                                                    int a12  = seed + 12;
                                                    total   += a12;
                                                    {
                                                        int a13  = seed + 13;
                                                        total   += a13;
                                                        {
                                                            int a14 = seed + 14;
                                                            total += a14;
                                                            {
                                                                int a15 =
                                                                    seed + 15;
                                                                total += a15;
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return total;
}

// 9+ integer params: the 9th+ are stack-passed, living at small positive bp
// offsets. MARKR/MARKW/CHKL on these must not disturb live register copies
// carrying the earlier register-passed args.
static int many_params(int a, int b, int c, int d, int e, int f, int g, int h,
                       int i, int j, int k) {
    int live  = a + b; // kept live across every stack-param access below
    live     += c + d + e + f + g + h;
    live     += i;     // 9th param: stack-passed
    live     += j;     // 10th
    live     += k;     // 11th
    return live;
}

// Escaping aggregate local forces a STKTAG emission right after its LEA3
// base; a live scalar computed alongside it must survive.
static int escaping_array(int seed) {
    int arr[8];
    for (int idx = 0; idx < 8; idx++)
        arr[idx] = seed + idx;
    int live = seed * 2;
    int sum  = 0;
    for (int idx = 0; idx < 8; idx++)
        sum += arr[idx];
    return sum + live;
}

int main(void) {
    if (nested_scopes(0) != 120)
        return 1; // 1+2+...+15
    if (many_params(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11) != 66)
        return 2;
    if (escaping_array(1) != (1 + 2 + 3 + 4 + 5 + 6 + 7 + 8) + 2)
        return 3;
    return 42;
}
