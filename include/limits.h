/* limits.h - integer type limits for CCCC C compiler */

#ifndef __LIMITS_H
#define __LIMITS_H

#define CHAR_BIT  8

#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255

/* Plain char is signed on every CCCC platform (ty_char, src/type.c), and */
/* under -c=native the host cc is always given -fsigned-char unconditionally */
/* (run_native_backend(), src/main.c, #1064) -- so CHAR_MIN/CHAR_MAX line up */
/* with SCHAR_MIN/SCHAR_MAX on both the VM and native paths. Defining these */
/* in terms of the signed macros (rather than restating the literals) makes */
/* that invariant self-documenting; if CCCC's char signedness policy or the */
/* -fsigned-char forwarding ever changes, these need to move together. */
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX

/* mblen/mbtowc/wctomb/mbstowcs/wcstombs (declared in <stdlib.h>) are real */
/* host FFI passthroughs (wrap_mblen/wrap_mbtowc/wrap_wctomb, */
/* src/stdlib/stdlib.c) -- a guest `char buf[MB_LEN_MAX]` is handed straight */
/* to the *host's* libc function, which writes up to the host's own */
/* MB_CUR_MAX bytes into it. Undersizing this is therefore a guest buffer */
/* overrun, same soundness class as jmp_buf's sizing (see include/setjmp.h); */
/* oversizing is harmless. Measured directly (not from memory, see */
/* feedback_verify_libc_signatures_linux), all four supported host combos: */
/*   - macOS arm64:     6 */
/*   - macOS x86_64:    6 */
/*   - glibc x86_64:    16 */
/*   - glibc aarch64:   16 */
/* 16 covers every measured host. */
#define MB_LEN_MAX 16

#define SHRT_MIN   (-32768)
#define SHRT_MAX   32767
#define USHRT_MAX  65535

#define INT_MIN    (-2147483648)
#define INT_MAX    2147483647
#define UINT_MAX   4294967295U

#define LONG_MIN   (-9223372036854775807L - 1)
#define LONG_MAX   9223372036854775807L
#define ULONG_MAX  18446744073709551615UL

#define LLONG_MIN  (-9223372036854775807LL - 1)
#define LLONG_MAX  9223372036854775807LL
#define ULLONG_MAX 18446744073709551615ULL

/* C23: maximum supported _BitInt width */
#define BITINT_MAXWIDTH 65535

#endif /* __LIMITS_H */
