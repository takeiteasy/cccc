// #1261 fixture: a declaration-only header for a runtime-only support module.
// Routed `#include @shared` from the test so its prototypes are visible to
// both runtime code and the comptime pass. None of these are meant to be
// *called* from [[cccc::comptime]] code -- they exist only so the comptime
// program ends up holding bodyless prototypes it never uses, the shape that
// used to crash cccc (#1261): every one was speculatively forwarded, and the
// ones whose bodies touch libc (rt_log) or a file-static (rt_touch) could
// not parse in the isolated comptime context, leaving a half-built error AST
// that detonated in -c=native Step-2 codegen.
#ifndef COMPTIME_SHARED_RT_1261_H
#define COMPTIME_SHARED_RT_1261_H

int rt_add(int a, int b);
int rt_touch(void);
int rt_log(const char *msg);

#endif
