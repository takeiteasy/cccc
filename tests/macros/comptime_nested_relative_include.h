#ifndef COMPTIME_NESTED_RELATIVE_INCLUDE_H
#define COMPTIME_NESTED_RELATIVE_INCLUDE_H
// Fixture for ticket #684: a plain #include (no route annotation) that
// appears inside an open #pragma cccc comptime begin...end block inherits
// the comptime-only context and must also resolve relative to this file.
int comptime_nested_quadruple(int n) {
    return n * 4;
}
#endif
