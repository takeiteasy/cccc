// embed.h -- link a file's bytes into the binary with the assembler's
// `.incbin`, as a NUL-terminated read-only array.
//
//   EMBED_DECL(sym, "include/stdio.h")   // at file scope
//
// declares `extern const char sym[]` (hidden) holding the file followed by a
// 0 byte. The path is resolved by the assembler against the working
// directory, so the compiler must run from the repository root.
//
// TODO(#1497): replace with `#embed` once the toolchain floor (gcc 13 / clang 18
// today) supports it.

#ifndef CCCC_EMBED_H
#define CCCC_EMBED_H

#ifdef __APPLE__
#define EMBED_SECTION    ".pushsection __TEXT,__const\n"
#define EMBED_PREFIX     "_" // Mach-O C symbols carry a leading underscore
#define EMBED_VISIBILITY(name) ".private_extern " name "\n"
#else
#define EMBED_SECTION    ".pushsection .rodata\n"
#define EMBED_PREFIX     ""
#define EMBED_VISIBILITY(name) ".globl " name "\n.hidden " name "\n"
#endif

#define EMBED_ASM(sym, path)                                                   \
    __asm__(EMBED_SECTION EMBED_VISIBILITY(EMBED_PREFIX #sym)                  \
                EMBED_PREFIX #sym ":\n.incbin \"" path "\"\n.byte 0\n"        \
            ".popsection\n")

#define EMBED_DECL(sym, path)                                                  \
    extern const char sym[] __attribute__((visibility("hidden")));             \
    EMBED_ASM(sym, path)

#endif
