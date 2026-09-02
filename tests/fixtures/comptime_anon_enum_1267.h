// #1267 fixture: a header holding an *anonymous* `typedef enum { ... } Name;`
// alongside a declaration-only helper. It is pulled into the comptime program
// two ways at once by test_comptime_anon_enum_typedef_1267.c -- textually via
// `#include @comptime`, and (its helper's body) forwarded on demand from
// comptime_anon_enum_1267_mod.c which #includes it the ordinary way. The
// anonymous enum typedef must dedup cleanly across those two routes.
#ifndef COMPTIME_ANON_ENUM_1267_H
#define COMPTIME_ANON_ENUM_1267_H
typedef enum { V_A, V_B, V_C } EKind;
int helper(int n);
#endif
