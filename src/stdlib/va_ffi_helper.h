// Bridge between cccc's va_list layout and host libffi variadic calls.
//
// cccc's va_list (see include/stdarg.h) is:
//   struct { char *reg_ptr; char *stack_ptr; int reg_count; }
// which is incompatible with the host va_list. When cccc-compiled code
// forwards a va_list to a v*-family wrapper (vprintf, vsprintf, ...), the
// wrapper receives a pointer to cccc's va_list struct as an int64 argument.
//
// This helper extracts individual args from that struct using cccc's own
// va_arg semantics, then dispatches them to the real host function via
// ffi_prep_cif_var, which builds a genuine host variadic frame so the
// callee's own va_start/va_arg work correctly. (#407)

#ifndef CCCC_VA_FFI_HELPER_H
#define CCCC_VA_FFI_HELPER_H

#include <ffi.h>
#include <stdint.h>
#include <string.h>

#define CCCC_VA_MAX_ARGS 64

// Mirrors the va_list struct layout from include/stdarg.h.
typedef struct {
    char *reg_ptr;
    char *stack_ptr;
    int   reg_count;
} cccc_va_list_t;

#define CCCC_VAARG_INT    0  // int64 / pointer slot
#define CCCC_VAARG_DOUBLE 1  // double slot

// Extract the next integer/pointer slot from cccc's va_list.
static inline int64_t cccc_va_next_i64(cccc_va_list_t *va) {
    if (va->reg_count > 0) {
        va->reg_count--;
        va->reg_ptr -= 8;
        return *(int64_t *)(va->reg_ptr + 8);
    }
    int64_t v = *(int64_t *)va->stack_ptr;
    va->stack_ptr += 8;
    return v;
}

// Extract the next double slot from cccc's va_list.
static inline double cccc_va_next_f64(cccc_va_list_t *va) {
    if (va->reg_count > 0) {
        va->reg_count--;
        va->reg_ptr -= 8;
        return *(double *)(va->reg_ptr + 8);
    }
    double v = *(double *)va->stack_ptr;
    va->stack_ptr += 8;
    return v;
}

// Parse a printf-family format string and classify each variadic argument.
// * args are int64; all float/double conversions are DOUBLE; everything else
// (d i u o x X c s p n b B and unknowns) is INT.
// Returns the count of variadic args found (capped at max_args).
static int __attribute__((unused))
cccc_parse_printf_fmt(const char *fmt, int *types, int max_args) {
    int n = 0;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%')
            continue;
        p++;
        if (!*p || *p == '%')
            continue;

        // Flags: - + space # 0
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0')
            p++;
        if (!*p) break;

        // Width: * or digits
        if (*p == '*') {
            if (n < max_args) types[n++] = CCCC_VAARG_INT;
            p++;
        } else {
            while (*p >= '0' && *p <= '9') p++;
        }
        if (!*p) break;

        // Precision: .* or .digits
        if (*p == '.') {
            p++;
            if (*p == '*') {
                if (n < max_args) types[n++] = CCCC_VAARG_INT;
                p++;
            } else {
                while (*p >= '0' && *p <= '9') p++;
            }
        }
        if (!*p) break;

        // Length modifiers (consume; all floats are passed as double slots).
        // H/D/DD (#829, _Decimal32/64/128) are consumed here too, but fall
        // through to the 'default' INT/pointer classification below same as
        // any other conversion -- a decimal variadic argument is always
        // passed by pointer (gen_decimal_arg_ptr), i.e. already exactly an
        // int64 slot, so no separate CCCC_VAARG_* class is needed for it.
        while (*p == 'h' || *p == 'l' || *p == 'j' || *p == 'z' ||
               *p == 't' || *p == 'L' || *p == 'H' || *p == 'D')
            p++;
        if (!*p) break;

        if (n < max_args) {
            switch (*p) {
            case 'f': case 'F': case 'e': case 'E':
            case 'g': case 'G': case 'a': case 'A':
                types[n++] = CCCC_VAARG_DOUBLE;
                break;
            default:
                // d i u o x X c s p n b B [ and unknowns
                types[n++] = CCCC_VAARG_INT;
                break;
            }
        }
        // The outer for loop's p++ advances past the conversion char.
    }
    return n;
}

// Parse a scanf-family format string and count output pointer arguments.
// Suppressed '*' conversions consume no output pointer.
// All non-suppressed args are pointers → CCCC_VAARG_INT.
static int __attribute__((unused))
cccc_parse_scanf_fmt(const char *fmt, int *types, int max_args) {
    int n = 0;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%')
            continue;
        p++;
        if (!*p || *p == '%')
            continue;

        int suppress = (*p == '*');
        if (suppress) p++;
        if (!*p) break;

        while (*p >= '0' && *p <= '9') p++;   // width
        if (!*p) break;

        // Length modifiers (H/D/DD, #829, consumed the same way as the
        // printf parser above)
        while (*p == 'h' || *p == 'l' || *p == 'j' || *p == 'z' ||
               *p == 't' || *p == 'L' || *p == 'H' || *p == 'D')
            p++;
        if (!*p) break;

        // Scanset: skip to closing ]
        if (*p == '[') {
            p++;
            if (*p == '^') p++;
            if (*p == ']') p++;       // literal ] as first scanset member
            while (*p && *p != ']') p++;
            // p is now at ']'; the outer for loop's p++ skips it
        }

        if (!suppress && n < max_args)
            types[n++] = CCCC_VAARG_INT;
    }
    return n;
}

// Extract n args from cccc's va_list according to types[].
// Stores raw 8-byte values in vals[].
static void cccc_va_extract(cccc_va_list_t *va, const int *types, int n,
                             int64_t *vals) {
    for (int i = 0; i < n; i++) {
        if (types[i] == CCCC_VAARG_DOUBLE) {
            double d = cccc_va_next_f64(va);
            memcpy(&vals[i], &d, sizeof d);
        } else {
            vals[i] = cccc_va_next_i64(va);
        }
    }
}

// Call a host variadic function via libffi.
// fixed_vals[0..num_fixed-1] are the non-variadic args (all int64).
// types[0..n-1] / vals[0..n-1] describe the variadic portion.
// Returns the sint64 return value.
static long long cccc_ffi_call_variadic(void *func_ptr,
                                        int num_fixed, const int64_t *fixed_vals,
                                        int n, const int *types,
                                        const int64_t *vals) {
    int total = num_fixed + n;
    // Fixed-size arrays; 8 fixed params + CCCC_VA_MAX_ARGS variadic is always enough.
    ffi_type *arg_type_buf[CCCC_VA_MAX_ARGS + 8];
    void     *arg_ptr_buf[CCCC_VA_MAX_ARGS + 8];

    for (int i = 0; i < num_fixed; i++) {
        arg_type_buf[i] = &ffi_type_sint64;
        arg_ptr_buf[i]  = (void *)&fixed_vals[i];
    }
    for (int i = 0; i < n; i++) {
        arg_type_buf[num_fixed + i] = (types[i] == CCCC_VAARG_DOUBLE)
                                       ? &ffi_type_double : &ffi_type_sint64;
        arg_ptr_buf[num_fixed + i]  = (void *)&vals[i];
    }

    ffi_cif cif;
    ffi_prep_cif_var(&cif, FFI_DEFAULT_ABI, (unsigned)num_fixed,
                     (unsigned)total, &ffi_type_sint64, arg_type_buf);

    long long result = 0;
    ffi_call(&cif, FFI_FN(func_ptr), &result, arg_ptr_buf);
    return result;
}

#endif // CCCC_VA_FFI_HELPER_H
