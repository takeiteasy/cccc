// shim_table.c -- the -c=native support shims (src/shims.h), embedded into the
// binary. Each chunk's file is `.incbin`'d, so a missing file is a build error.
// The compiler must run from the repository root (see src/embed.h).

#include "./embed.h"
#include "./shims.h"

#define EMBED_SHIM(group, name)                                                \
    EMBED_ASM(CCCC_SHIM_##group##_##name, "src/shims/" #group "/" #name ".c");
SHIMS(EMBED_SHIM)
#undef EMBED_SHIM
EMBED_ASM(CCCC_SHIM_wide_bitint_runtime, "src/stdlib/wide_bitint/runtime.c");
EMBED_ASM(CCCC_SHIM_c23_math_runtime, "src/stdlib/c23_math/runtime.c");
EMBED_ASM(CCCC_SHIM_c23_mem_runtime, "src/stdlib/c23_mem/runtime.c");
