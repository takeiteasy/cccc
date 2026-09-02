// #1257 fixture: a 40-deep transitive call chain. Passed to cccc as a separate
// command-line input; comptime code calls deep_chain_1, and the forwarding
// sweep pulls in deep_chain_2..40 one per fixed-point round (each body reveals
// the next reference). Ordered callee-before-caller so no forward decls are
// needed.
#include "comptime_deep_chain_1257.h"

int deep_chain_40(int n) { return n + 1; }
int deep_chain_39(int n) { return deep_chain_40(n) + 1; }
int deep_chain_38(int n) { return deep_chain_39(n) + 1; }
int deep_chain_37(int n) { return deep_chain_38(n) + 1; }
int deep_chain_36(int n) { return deep_chain_37(n) + 1; }
int deep_chain_35(int n) { return deep_chain_36(n) + 1; }
int deep_chain_34(int n) { return deep_chain_35(n) + 1; }
int deep_chain_33(int n) { return deep_chain_34(n) + 1; }
int deep_chain_32(int n) { return deep_chain_33(n) + 1; }
int deep_chain_31(int n) { return deep_chain_32(n) + 1; }
int deep_chain_30(int n) { return deep_chain_31(n) + 1; }
int deep_chain_29(int n) { return deep_chain_30(n) + 1; }
int deep_chain_28(int n) { return deep_chain_29(n) + 1; }
int deep_chain_27(int n) { return deep_chain_28(n) + 1; }
int deep_chain_26(int n) { return deep_chain_27(n) + 1; }
int deep_chain_25(int n) { return deep_chain_26(n) + 1; }
int deep_chain_24(int n) { return deep_chain_25(n) + 1; }
int deep_chain_23(int n) { return deep_chain_24(n) + 1; }
int deep_chain_22(int n) { return deep_chain_23(n) + 1; }
int deep_chain_21(int n) { return deep_chain_22(n) + 1; }
int deep_chain_20(int n) { return deep_chain_21(n) + 1; }
int deep_chain_19(int n) { return deep_chain_20(n) + 1; }
int deep_chain_18(int n) { return deep_chain_19(n) + 1; }
int deep_chain_17(int n) { return deep_chain_18(n) + 1; }
int deep_chain_16(int n) { return deep_chain_17(n) + 1; }
int deep_chain_15(int n) { return deep_chain_16(n) + 1; }
int deep_chain_14(int n) { return deep_chain_15(n) + 1; }
int deep_chain_13(int n) { return deep_chain_14(n) + 1; }
int deep_chain_12(int n) { return deep_chain_13(n) + 1; }
int deep_chain_11(int n) { return deep_chain_12(n) + 1; }
int deep_chain_10(int n) { return deep_chain_11(n) + 1; }
int deep_chain_9(int n) { return deep_chain_10(n) + 1; }
int deep_chain_8(int n) { return deep_chain_9(n) + 1; }
int deep_chain_7(int n) { return deep_chain_8(n) + 1; }
int deep_chain_6(int n) { return deep_chain_7(n) + 1; }
int deep_chain_5(int n) { return deep_chain_6(n) + 1; }
int deep_chain_4(int n) { return deep_chain_5(n) + 1; }
int deep_chain_3(int n) { return deep_chain_4(n) + 1; }
int deep_chain_2(int n) { return deep_chain_3(n) + 1; }
int deep_chain_1(int n) { return deep_chain_2(n) + 1; }
