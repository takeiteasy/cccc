// posix_lang.c -- nl_langinfo(_l), vsyslog's va_list-forwarding parser,
// and the macOS strfmon ASan suppression (#946 split of posix.c).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

// nl_langinfo() (#807) -- nl_item values diverge wildly between hosts:
// macOS uses a flat 0-56 sequence (which CCCC's canonical numbering,
// include/langinfo.h, copies verbatim), glibc packs
// (category << 16) | index. The DAY_/ABDAY_/MON_/ABMON_ families are each
// a contiguous run under both numbering schemes, so they translate via
// range arithmetic rather than 46 individual case labels; everything
// else goes through a small table. Anything unrecognized returns ""
// rather than forwarding a bogus nl_item to the host.
//
// The case/range bounds below are written as bare integer literals
// (CCCC's own canonical numbering, matching include/langinfo.h) rather
// than the CODESET/DAY_1/etc. macro names -- this file includes the
// *host's* real <langinfo.h> (there is no -Iinclude in this TU's build,
// see the Makefile), so on Linux those names already expand to glibc's
// real values (e.g. CODESET is 14, not 0) and would silently compare the
// guest's canonical input against the wrong number.
// Shared by wrap_nl_langinfo and wrap_nl_langinfo_l (#820) so the
// translation table/range arithmetic lives in exactly one place.
// *found is set to 0 for an unrecognized canonical item (caller returns ""
// without touching the host at all), 1 otherwise.
// #1148 (found, not fixed, while implementing #1146): *found is set here,
// before the #ifdef, so the #else arm's own *found = 0 for an unrecognized
// canonical item only ever takes effect on non-Apple hosts -- on macOS this
// line already committed *found = 1, so an out-of-range nl_item is
// forwarded to the host raw instead of short-circuiting to "" the way this
// function's own comment above promises.
static nl_item guest_to_host_nl_item(nl_item guest_item, int *found) {
    *found = 1;
#ifdef __APPLE__
    return guest_item;
#else
    long v = (long)guest_item;
    long host_item;
    switch (v) {
        case 0:
            host_item = 14;
            break;                       // CODESET
        case 1:
            host_item = 131112;
            break;                       // D_T_FMT
        case 2:
            host_item = 131113;
            break;                       // D_FMT
        case 3:
            host_item = 131114;
            break;                       // T_FMT
        case 4:
            host_item = 131115;
            break;                       // T_FMT_AMPM
        case 5:
            host_item = 131110;
            break;                       // AM_STR
        case 6:
            host_item = 131111;
            break;                       // PM_STR
        case 50:
            host_item = 65536;
            break;                       // RADIXCHAR
        case 51:
            host_item = 65537;
            break;                       // THOUSEP
        case 52:
            host_item = 327680;
            break;                       // YESEXPR
        case 53:
            host_item = 327681;
            break;                       // NOEXPR
        case 56:
            host_item = 262159;
            break;                       // CRNCYSTR
        default:
            if (v >= 7 && v <= 13)       // DAY_1..DAY_7
                host_item = 131079 + (v - 7);
            else if (v >= 14 && v <= 20) // ABDAY_1..ABDAY_7
                host_item = 131072 + (v - 14);
            else if (v >= 21 && v <= 32) // MON_1..MON_12
                host_item = 131098 + (v - 21);
            else if (v >= 33 && v <= 44) // ABMON_1..ABMON_12
                host_item = 131086 + (v - 33);
            else {
                *found = 0;
                return (nl_item)0;
            }
            break;
    }
    return (nl_item)host_item;
#endif
}

static char *wrap_nl_langinfo(nl_item guest_item) {
    int     found;
    nl_item host_item = guest_to_host_nl_item(guest_item, &found);
    if (!found)
        return "";
    return nl_langinfo(host_item);
}

// nl_langinfo_l() (#820) -- same canonical nl_item translation as
// nl_langinfo() above, against an explicit locale_t instead of the
// process-global/per-thread locale.
static char *wrap_nl_langinfo_l(nl_item guest_item, locale_t loc) {
    int     found;
    nl_item host_item = guest_to_host_nl_item(guest_item, &found);
    if (!found)
        return "";
    return nl_langinfo_l(host_item, loc);
}

// ---------------------------------------------------------------------------
// vsyslog() (#803) -- forwards a captured cccc va_list to the host's real
// variadic syslog() via ffi_prep_cif_var, same technique as
// format_printf.c's wrap_cccc_vprintf family (#407). Unlike a plain printf
// forward, syslog's "%m" conversion (strerror(errno)) consumes zero
// variadic args -- the shared cccc_parse_printf_fmt classifies unknown
// conversions as taking one INT arg, which would misalign extraction here,
// so this uses its own parser that special-cases 'm' as a no-arg literal.
// va_ffi_helper.h's __attribute__((unused)) markers rely on __attribute__
// actually expanding; internal.h (included above) #defines __attribute__(x)
// to nothing for this TU, so cccc_parse_printf_fmt/cccc_parse_scanf_fmt
// (unused here -- this file only needs cccc_va_extract/cccc_ffi_call_variadic
// and its own syslog-specific parser below) would otherwise warn.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "va_ffi_helper.h"
#pragma GCC diagnostic pop

static int cccc_parse_syslog_fmt(const char *fmt, int *types, int max_args) {
    int n = 0;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%')
            continue;
        p++;
        if (!*p || *p == '%' || *p == 'm')
            continue;

        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0')
            p++;
        if (!*p)
            break;

        if (*p == '*') {
            if (n < max_args)
                types[n++] = CCCC_VAARG_INT;
            p++;
        } else {
            while (*p >= '0' && *p <= '9')
                p++;
        }
        if (!*p)
            break;

        if (*p == '.') {
            p++;
            if (*p == '*') {
                if (n < max_args)
                    types[n++] = CCCC_VAARG_INT;
                p++;
            } else {
                while (*p >= '0' && *p <= '9')
                    p++;
            }
        }
        if (!*p)
            break;

        while (*p == 'h' || *p == 'l' || *p == 'j' || *p == 'z' || *p == 't' ||
               *p == 'L')
            p++;
        if (!*p)
            break;

        if (n < max_args) {
            switch (*p) {
                case 'f':
                case 'F':
                case 'e':
                case 'E':
                case 'g':
                case 'G':
                case 'a':
                case 'A':
                    types[n++] = CCCC_VAARG_DOUBLE;
                    break;
                default:
                    types[n++] = CCCC_VAARG_INT;
                    break;
            }
        }
    }
    return n;
}

static long long wrap_vsyslog(long long priority, long long fmt,
                              long long va_ptr) {
    CCCC_VA_LOCAL(va, va_ptr);
    int types[CCCC_VA_MAX_ARGS];
    int n = cccc_parse_syslog_fmt((const char *)fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = {(int64_t)priority, (int64_t)fmt};
    return cccc_ffi_call_variadic((void *)syslog, 2, fixed, n, types, vals);
}

// Host-global accessors (#736, __cccc_errno_ptr/__cccc_optarg_ptr/etc.) are
// registered in posix_io.c, next to getopt() itself.

// #841 -- macOS libc's _strfmon() (see the "monetary.h" registration below)
// over-reads its own internal scratch allocation under AddressSanitizer:
// it mallocs a 15-byte buffer and then memcpy()s 9 bytes starting at offset
// 15 of it. Confirmed with a standalone 6-line clang -fsanitize=address
// program with zero CCCC involvement -- every conversion directive
// ("%n", "%i", "%!n", "%.2n", "%#5n") triggers it, a format with no
// directive doesn't. It's benign against a real allocator (15 bytes rounds
// up to a 16-byte malloc bucket, so the bytes read are mapped) and only
// aborts against ASan's exact-size redzones. Confirmed clean on
// Linux/glibc. Suppress just this frame under ASan rather than avoiding
// strfmon(): reimplementing it would go against the no-lossy-POSIX-
// emulation policy for a bug that's harmless outside ASan. A user's own
// ASAN_OPTIONS=suppressions=<file> merges with this list rather than
// replacing it (confirmed empirically), so this doesn't hide anything from
// someone actively debugging with a custom suppression file.
#if defined(__APPLE__) &&                                                      \
    (defined(__SANITIZE_ADDRESS__) ||                                          \
     (defined(__has_feature) && __has_feature(address_sanitizer)))
const char *__asan_default_suppressions(void) {
    return "interceptor_via_fun:_strfmon\n";
}
#endif

void register_posix_lang_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "iconv_open", (void *)iconv_open, 2, 0);
    cc_register_cfunc(vm, "iconv", (void *)iconv, 5, 0);
    cc_register_cfunc(vm, "iconv_close", (void *)iconv_close, 1, 0);
    cc_register_cfunc(vm, "nl_langinfo", (void *)wrap_nl_langinfo, 1, 0);
    cc_register_cfunc(vm, "nl_langinfo_l", (void *)wrap_nl_langinfo_l, 2, 0);
    cc_register_cfunc(vm, "catopen", (void *)catopen, 2, 0);
    cc_register_cfunc(vm, "catgets", (void *)catgets, 4, 0);
    cc_register_cfunc(vm, "catclose", (void *)catclose, 1, 0);

    // syslog.h (#803) -- LOG_* constants are identical on both platforms so
    // the header needs no guards. syslog() is registered as a real variadic
    // FFI function (not a va_list-forwarding wrapper): codegen computes
    // double_arg_mask per call-site from the caller's static argument types
    // (src/codegen.c:5405-5415), so this correctly threads through %f
    // arguments the same way a direct printf() call does -- no format-string
    // parsing required here, unlike the vprintf-family wrappers, which only
    // exist because a captured va_list has already erased those static types.
    cc_register_cfunc(vm, "openlog", (void *)openlog, 3, 0);
    cc_register_cfunc(vm, "closelog", (void *)closelog, 0, 0);
    cc_register_cfunc(vm, "setlogmask", (void *)setlogmask, 1, 0);
    cc_register_variadic_cfunc(vm, "syslog", (void *)syslog, 2, 0);
    cc_register_cfunc(vm, "vsyslog", (void *)wrap_vsyslog, 3, 0);

    // monetary.h (#808) -- strfmon is variadic with double arguments, but
    // (like syslog above) is a real, non-va_list-forwarding call site, so
    // the plain host function registers directly and codegen's per-call-
    // site double_arg_mask threads the double through correctly -- no
    // split-format host-side reimplementation needed (confirmed
    // empirically: a real double argument round-trips through a "%n"
    // conversion correctly). On macOS, this host strfmon() has an internal
    // over-read that only ASan notices -- see the __asan_default_suppressions
    // hook above (#841).
    cc_register_variadic_cfunc(vm, "strfmon", (void *)strfmon, 3, 0);
    // strfmon_l (#820) -- locale_t sits before the format string, so this
    // is 4 fixed args, not 3 like strfmon above.
    cc_register_variadic_cfunc(vm, "strfmon_l", (void *)strfmon_l, 4, 0);
}

#else
void register_posix_lang_functions(VirtualMachine *vm) {
    (void)vm;
}
#endif
