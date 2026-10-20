// C23 strtol/strtoll/strtoul/strtoull accept an optional "0b"/"0B" prefix when
// base is 0 or 2; host libcs differ (macOS and glibc before 2.38 reject it).
// Compiled into the VM by src/stdlib/stdlib.c and emitted verbatim as
// -c=native/-m shim text, so both run the same code. Guest calls are renamed to
// __cccc_native_<name>.
//
// The text includes no headers: the host <stdlib.h> the guest included declares
// strtoll/strtoull, and everything else goes through __builtin_*.

#define __CCCC_BIN_SPACE(c) ((c) == ' ' || ((c) >= '\t' && (c) <= '\r'))

// Copies nptr without the "0b" after the sign and white space; returns NULL if
// nptr has no such prefix (or on allocation failure). *skip is the offset of
// the removed "0b" in nptr.
static inline char *__cccc_bin_strip(const char *nptr, int base,
                                     __SIZE_TYPE__ *skip) {
    if (base != 0 && base != 2)
        return 0;
    const char *p = nptr;
    while (__CCCC_BIN_SPACE((unsigned char)*p))
        p++;
    const char *digits = p;
    if (*digits == '+' || *digits == '-')
        digits++;
    if (!(digits[0] == '0' && (digits[1] == 'b' || digits[1] == 'B') &&
          (digits[2] == '0' || digits[2] == '1')))
        return 0;
    __SIZE_TYPE__ prefix_len = (__SIZE_TYPE__)(digits - nptr);
    __SIZE_TYPE__ rest_len   = __builtin_strlen(digits + 2);
    char         *tmp        = __builtin_malloc(prefix_len + rest_len + 1);
    if (!tmp)
        return 0;
    __builtin_memcpy(tmp, nptr, prefix_len);
    __builtin_memcpy(tmp + prefix_len, digits + 2, rest_len + 1);
    *skip = prefix_len;
    return tmp;
}

static inline long long __cccc_native_strtoll(const char *nptr, char **endptr,
                                              int base) {
    __SIZE_TYPE__ skip;
    char         *tmp = __cccc_bin_strip(nptr, base, &skip);
    if (!tmp)
        return strtoll(nptr, endptr, base);
    char     *tmp_end;
    long long result = strtoll(tmp, &tmp_end, 2);
    if (endptr)
        *endptr = (char *)nptr + (__SIZE_TYPE__)(tmp_end - tmp) + 2;
    __builtin_free(tmp);
    return result;
}

static inline unsigned long long
__cccc_native_strtoull(const char *nptr, char **endptr, int base) {
    __SIZE_TYPE__ skip;
    char         *tmp = __cccc_bin_strip(nptr, base, &skip);
    if (!tmp)
        return strtoull(nptr, endptr, base);
    char              *tmp_end;
    unsigned long long result = strtoull(tmp, &tmp_end, 2);
    if (endptr)
        *endptr = (char *)nptr + (__SIZE_TYPE__)(tmp_end - tmp) + 2;
    __builtin_free(tmp);
    return result;
}

static inline long __cccc_native_strtol(const char *nptr, char **endptr,
                                        int base) {
    return (long)__cccc_native_strtoll(nptr, endptr, base);
}

static inline unsigned long __cccc_native_strtoul(const char *nptr,
                                                  char **endptr, int base) {
    return (unsigned long)__cccc_native_strtoull(nptr, endptr, base);
}
