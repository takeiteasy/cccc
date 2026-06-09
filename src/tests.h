/*
 JCC test framework header — injected automatically in --testing mode.
 Do not include this directly; use the --testing flag instead.
*/

#pragma once

void __jcc_assert(int cond, const char *expr, const char *file, int line);
void __jcc_assert_eq(long long a, long long b, const char *as, const char *bs,
                     const char *file, int line);
void __jcc_assert_neq(long long a, long long b, const char *as, const char *bs,
                      const char *file, int line);
void __jcc_assert_null(const void *p, const char *ps,
                       const char *file, int line);
void __jcc_assert_not_null(const void *p, const char *ps,
                           const char *file, int line);
void __jcc_assert_streq(const char *a, const char *b,
                        const char *as, const char *bs,
                        const char *file, int line);

#define JCC_ASSERT(cond) \
    __jcc_assert(!!(cond), #cond, __FILE__, __LINE__)
#define JCC_ASSERT_EQ(a, b) \
    __jcc_assert_eq((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)
#define JCC_ASSERT_NEQ(a, b) \
    __jcc_assert_neq((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)
#define JCC_ASSERT_NULL(p) \
    __jcc_assert_null((const void *)(p), #p, __FILE__, __LINE__)
#define JCC_ASSERT_NOT_NULL(p) \
    __jcc_assert_not_null((const void *)(p), #p, __FILE__, __LINE__)
#define JCC_ASSERT_STREQ(a, b) \
    __jcc_assert_streq((a), (b), #a, #b, __FILE__, __LINE__)
