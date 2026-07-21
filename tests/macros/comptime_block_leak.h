#ifndef COMPTIME_BLOCK_LEAK_H
#define COMPTIME_BLOCK_LEAK_H
// Fixture for ticket #683: opens a comptime block and never closes it before
// EOF, so the including file auto-closes it and emits -Wcomptime-block-leak.
#pragma cccc comptime begin
int leak_double(int n) {
    return n * 2;
}
#endif
