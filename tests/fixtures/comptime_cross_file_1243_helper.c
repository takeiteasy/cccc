// #1243 fixture: the definition for comptime_cross_file_1243_helper.h. Passed
// to cccc as a separate command-line input; its body is forwarded into the
// comptime program on demand because comptime code below calls it.
#include "comptime_cross_file_1243_helper.h"

static int helper_triple(int n) {
    return n * 3;
}

int helper_double(int n) {
    return helper_triple(n) - n;
}
