/* #1258: declaration-only externs, brought into both the runtime TU and the
 * comptime program via `#include @shared`. A Quote() template in comptime
 * code must be able to name them -- they have no storage of their own in
 * either program, so there is no shadow-copy hazard for the #1250 guard to
 * catch. */
#ifndef QUOTE_SHARED_EXTERN_GLOBAL_1258_H
#define QUOTE_SHARED_EXTERN_GLOBAL_1258_H

extern int shared_base_1258;
extern int shared_bump_1258;

#endif
