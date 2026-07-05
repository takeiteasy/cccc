/* Test that __attribute__((format(...))) accepts GNU/Clang alternate spellings.
   macOS SDK headers use __printf__, gnu_printf, __scanf__, strftime, os_log
   via __printflike/__scanflike macros from <sys/cdefs.h>. */
__attribute__((format(__printf__, 1, 2))) int my_printf(const char *fmt, ...);
__attribute__((format(gnu_printf, 1, 2))) int my_gprintf(const char *fmt, ...);
__attribute__((format(__printf0__, 1, 2))) int my_printf0(const char *fmt, ...);
__attribute__((format(__scanf__, 1, 2))) int my_scanf(const char *fmt, ...);
__attribute__((format(gnu_scanf, 1, 2))) int my_gscanf(const char *fmt, ...);
__attribute__((format(strftime, 3, 0))) int my_strftime(char *s, int n, const char *fmt, void *tm);
__attribute__((format(__strftime__, 3, 0))) int my_strftime2(char *s, int n, const char *fmt, void *tm);
__attribute__((format(os_log, 1, 2))) int my_oslog(const char *fmt, ...);
__attribute__((format(__os_log__, 1, 2))) int my_oslog2(const char *fmt, ...);

int main(void) { return 42; }
