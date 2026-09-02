// #1257 fixture: declaration-only header for comptime_deep_chain_1257.c. Only
// deep_chain_1 is named by comptime code; deep_chain_2..40 are forwarded
// transitively by the body-forwarding sweep, one per fixed-point round.
#ifndef COMPTIME_DEEP_CHAIN_1257_H
#define COMPTIME_DEEP_CHAIN_1257_H
int deep_chain_1(int n);
#endif
