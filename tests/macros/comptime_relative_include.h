#ifndef COMPTIME_RELATIVE_INCLUDE_H
#define COMPTIME_RELATIVE_INCLUDE_H
// Fixture for ticket #684: a plain header included via
// #include @comptime "..." (quoted, relative to the including file).
int comptime_relative_triple(int n) {
    return n * 3;
}
#endif
