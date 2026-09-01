// #1243 fixture: a conventional declaration-only header. The definition of
// helper_double lives in comptime_cross_file_1243_helper.c, compiled and
// passed alongside on the cccc command line -- not textually present in the
// translation unit that calls it from [[cccc::comptime]] code.
#ifndef COMPTIME_CROSS_FILE_1243_HELPER_H
#define COMPTIME_CROSS_FILE_1243_HELPER_H
int helper_double(int n);
#endif
