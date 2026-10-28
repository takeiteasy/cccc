// Wide (L/u/U) string literals used as expressions keep every element under
// -c=native.
#include <wchar.h>
#include <uchar.h>
#include <string.h>
static int u16eq(const char16_t *a, const char16_t *b) {
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}
static int u32eq(const char32_t *a, const char32_t *b) {
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}
int main(void) {
    wchar_t a[6] = L"hello";
    if (wcscmp(a, L"hello") != 0)
        return 1;
    if (wcscmp(a, L"hellp") == 0)
        return 2;
    if (wcslen(L"a\"b\\c\x41"
               L"bc") != 8)
        return 3;
    if (wcslen(L"éz") != 2 || wcscmp(L"éz", L"\xe9"
                                            L"z") != 0)
        return 4;
    if (!u16eq(u"héllo", u"héllo") || u16eq(u"hello", u"hellp"))
        return 5;
    if (!u32eq(U"h\U0001F600llo", U"h\U0001F600llo") || u32eq(U"x", U"y"))
        return 6;
    if (wcslen(L"") != 0 || wcslen(L"a\0b") != 1)
        return 7;
    return 42;
}
