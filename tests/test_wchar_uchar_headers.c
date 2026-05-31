#include <uchar.h>
#include <wchar.h>
#include <wctype.h>

int main(void) {
    wchar_t ws[] = L"abc";
    if (wcslen(ws) != 3) return 1;
    if (wctob(L'A') != 'A') return 2;
    if (!iswalpha(L'Z')) return 3;

    char16_t c16 = u'A';
    char32_t c32 = U'B';
    if (c16 != 'A') return 4;
    if (c32 != 'B') return 5;

    mbstate_t st = {0};
    char buf[8];
    if (c16rtomb(buf, c16, &st) < 1) return 6;

    return 42;
}
