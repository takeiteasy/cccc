// CCCC_FLAGS: --testing

// Archetypes CCCC does not check (CFString, NSString, os_trace, strfmon, ...)
// parse silently and leave the function unvalidated.

__attribute__((format(CFString, 1, 2))) int cf_log(const char *fmt, ...);
__attribute__((format(__NSString__, 1, 2))) int ns_log(const char *fmt, ...);
__attribute__((format(os_trace, 1, 2))) int trace_log(const char *fmt, ...);
__attribute__((format(strfmon, 3, 4))) int my_strfmon(char *s, int n,
                                                      const char *fmt, ...);
__attribute__((format(__syslog__, 2, 3))) void my_syslog(int         pri,
                                                         const char *fmt, ...);

#define CF_FORMAT_FUNCTION(F, A) __attribute__((format(CFString, F, A)))
CF_FORMAT_FUNCTION(1, 2) int macro_log(const char *fmt, ...);

__attribute__((format(printf, 1, 2))) int checked_printf(const char *fmt, ...) {
    return 0;
}

[[cccc::test(return = 42)]]
int test_unchecked_archetypes_parse(void) {
    return 42;
}

// An unchecked archetype never rejects arguments a printf one would.
int cf_log(const char *fmt, ...) {
    return 0;
}

[[cccc::test(return = 42)]]
int test_unchecked_archetype_skips_validation(void) {
    cf_log("%d", "not an int");
    return 42;
}
