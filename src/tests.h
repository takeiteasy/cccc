/*
 CCCC test framework header — injected automatically in --testing mode.
 Do not include this directly; use the --testing flag instead.
*/

#pragma once

void __cccc_assert(int cond, const char *expr, const char *file, int line);
void __cccc_assert_eq(long long a, long long b, const char *as, const char *bs,
                     const char *file, int line);
void __cccc_assert_neq(long long a, long long b, const char *as, const char *bs,
                      const char *file, int line);
void __cccc_assert_null(const void *p, const char *ps,
                       const char *file, int line);
void __cccc_assert_not_null(const void *p, const char *ps,
                           const char *file, int line);
void __cccc_assert_streq(const char *a, const char *b,
                        const char *as, const char *bs,
                        const char *file, int line);

#define CCCC_ASSERT(cond) \
    __cccc_assert(!!(cond), #cond, __FILE__, __LINE__)
#define CCCC_ASSERT_EQ(a, b) \
    __cccc_assert_eq((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)
#define CCCC_ASSERT_NEQ(a, b) \
    __cccc_assert_neq((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)
#define CCCC_ASSERT_NULL(p) \
    __cccc_assert_null((const void *)(p), #p, __FILE__, __LINE__)
#define CCCC_ASSERT_NOT_NULL(p) \
    __cccc_assert_not_null((const void *)(p), #p, __FILE__, __LINE__)
#define CCCC_ASSERT_STREQ(a, b) \
    __cccc_assert_streq((a), (b), #a, #b, __FILE__, __LINE__)
