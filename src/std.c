#include <string.h>

static const char std_Availability_h[] = {
#embed "../include/Availability.h"
, 0};

static const char std_assert_h[] = {
#embed "../include/assert.h"
, 0};

static const char std_ctype_h[] = {
#embed "../include/ctype.h"
, 0};

static const char std_errno_h[] = {
#embed "../include/errno.h"
, 0};

static const char std_float_h[] = {
#embed "../include/float.h"
, 0};

static const char std_inttypes_h[] = {
#embed "../include/inttypes.h"
, 0};

static const char std_limits_h[] = {
#embed "../include/limits.h"
, 0};

static const char std_math_h[] = {
#embed "../include/math.h"
, 0};

static const char std_reflection_h[] = {
#embed "../include/reflection.h"
, 0};

static const char std_setjmp_h[] = {
#embed "../include/setjmp.h"
, 0};

static const char std_stdalign_h[] = {
#embed "../include/stdalign.h"
, 0};

static const char std_stdarg_h[] = {
#embed "../include/stdarg.h"
, 0};

static const char std_stdatomic_h[] = {
#embed "../include/stdatomic.h"
, 0};

static const char std_stdbool_h[] = {
#embed "../include/stdbool.h"
, 0};

static const char std_stddef_h[] = {
#embed "../include/stddef.h"
, 0};

static const char std_stdint_h[] = {
#embed "../include/stdint.h"
, 0};

static const char std_stdio_h[] = {
#embed "../include/stdio.h"
, 0};

static const char std_stdlib_h[] = {
#embed "../include/stdlib.h"
, 0};

static const char std_stdnoreturn_h[] = {
#embed "../include/stdnoreturn.h"
, 0};

static const char std_string_h[] = {
#embed "../include/string.h"
, 0};

static const char std_sys_cdefs_h[] = {
#embed "../include/sys/cdefs.h"
, 0};

static const char std_time_h[] = {
#embed "../include/time.h"
, 0};

char *get_std_header(char *filename) {
    size_t filename_len = strlen(filename) + 1;
    if (strncmp(filename, "Availability.h", filename_len) == 0) return (char *)std_Availability_h;
    if (strncmp(filename, "assert.h", filename_len) == 0) return (char *)std_assert_h;
    if (strncmp(filename, "ctype.h", filename_len) == 0) return (char *)std_ctype_h;
    if (strncmp(filename, "errno.h", filename_len) == 0) return (char *)std_errno_h;
    if (strncmp(filename, "float.h", filename_len) == 0) return (char *)std_float_h;
    if (strncmp(filename, "inttypes.h", filename_len) == 0) return (char *)std_inttypes_h;
    if (strncmp(filename, "limits.h", filename_len) == 0) return (char *)std_limits_h;
    if (strncmp(filename, "math.h", filename_len) == 0) return (char *)std_math_h;
    if (strncmp(filename, "reflection.h", filename_len) == 0) return (char *)std_reflection_h;
    if (strncmp(filename, "setjmp.h", filename_len) == 0) return (char *)std_setjmp_h;
    if (strncmp(filename, "stdalign.h", filename_len) == 0) return (char *)std_stdalign_h;
    if (strncmp(filename, "stdarg.h", filename_len) == 0) return (char *)std_stdarg_h;
    if (strncmp(filename, "stdatomic.h", filename_len) == 0) return (char *)std_stdatomic_h;
    if (strncmp(filename, "stdbool.h", filename_len) == 0) return (char *)std_stdbool_h;
    if (strncmp(filename, "stddef.h", filename_len) == 0) return (char *)std_stddef_h;
    if (strncmp(filename, "stdint.h", filename_len) == 0) return (char *)std_stdint_h;
    if (strncmp(filename, "stdio.h", filename_len) == 0) return (char *)std_stdio_h;
    if (strncmp(filename, "stdlib.h", filename_len) == 0) return (char *)std_stdlib_h;
    if (strncmp(filename, "stdnoreturn.h", filename_len) == 0) return (char *)std_stdnoreturn_h;
    if (strncmp(filename, "string.h", filename_len) == 0) return (char *)std_string_h;
    if (strncmp(filename, "sys/cdefs.h", filename_len) == 0) return (char *)std_sys_cdefs_h;
    if (strncmp(filename, "time.h", filename_len) == 0) return (char *)std_time_h;
    return NULL;
}
