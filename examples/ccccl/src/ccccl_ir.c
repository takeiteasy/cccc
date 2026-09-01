/* ccccl_ir.c — definitions for ccccl_ir.h.
 *
 * Pure C. Compiled twice: once by plain `cc` (linked into the example
 * binaries), once inside the cccc comptime VM — src/ccccl_comptime.c pulls
 * the declarations in with `#include @comptime "ccccl_ir.h"` and cccc
 * forwards these bodies into the comptime program on demand (the .h/.c
 * split the cross-file comptime-call support exists for).
 */
#include "ccccl_ir.h"

void ccccl_plan_init(CccclPlan *p) {
    p->expr_count = 0;
    p->fn_count   = 0;
    p->sym_count  = 0;
    p->error[0]   = '\0';
    p->has_error  = 0;
}
