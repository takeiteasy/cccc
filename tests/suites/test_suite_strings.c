// CCCC_FLAGS: --testing
// Consolidated suite: string literals, character arrays, string functions
// Source tests: test_string_concat, test_string_functions, test_strings, test_strings_and_arrays, test_strings_comprehensive, test_unicode_comments, test_wchar_uchar_headers,
//   test_format_string_valid, test_format_string_complex, test_format_length_modifier_valid

#include <string.h>
#include <uchar.h>
#include <wchar.h>
#include <wctype.h>

// [from test_string_concat]
// Test string literal concatenation
// Tests automatic concatenation of adjacent string literals

// [from test_string_functions]
// Test string operations: length calculation, comparison helper

// Simple string length function

static int str_len(char *s) {
    int len = 0;
    while (s[len] != '\0') {
        len = len + 1;
    }
    return len;
}

// Simple string comparison (returns 1 if equal, 0 if not)

static int str_equal(char *s1, char *s2) {
    int i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) {
            return 0;
        }
        i = i + 1;
    }
    // Check both ended at same time
    if (s1[i] == '\0' && s2[i] == '\0') {
        return 1;
    }
    return 0;
}

// [from test_strings]
// Test string literals

// [from test_strings_and_arrays]
// Test combining strings and arrays

// [from test_strings_comprehensive]
// Comprehensive test for string literals

// [from test_unicode_comments]
// Test: Unicode characters in comments and string literals
// em dash — en dash – section sign § bullet • copyright © trademark ™
// French: café crêpe naïve résumé
// Greek: αβγδ
// CJK: 日本語
/* Block comment: §7.28 — C23 standard references with Unicode */

#pragma cccc suite begin "strings"

// test_string_concat
[[cccc::test(return = 42)]]
int test_string_concat(void) {
    char *s;
    
    // Test 1: Basic concatenation
    s = "Hello" " " "World";
    if (s[0] != 'H') return 1;
    if (s[6] != 'W') return 2;
    
    // Test 2: Multi-line concatenation
    s = "First"
        "Second";
    if (s[0] != 'F') return 3;
    if (s[5] != 'S') return 4;
    
    // Test 3: Empty string concatenation
    s = "" "test";
    if (s[0] != 't') return 5;
    
    // Test 4: Many concatenations
    s = "a" "b" "c" "d" "e" "f";
    if (s[0] != 'a') return 6;
    if (s[5] != 'f') return 7;
    
    // Test 5: Parenthesized concatenation
    s = ("one" "two");
    if (s[0] != 'o') return 8;
    if (s[3] != 't') return 9;
    
    return 42;  // Success
}

// test_string_functions
[[cccc::test(return = 42)]]
int test_string_functions(void) {
    // Test string length
    char *test1 = "Hello";
    if (str_len(test1) != 5) return 1;
    
    char *test2 = "";
    if (str_len(test2) != 0) return 2;
    
    char *test3 = "A";
    if (str_len(test3) != 1) return 3;
    
    char *test4 = "This is a longer string!";
    if (str_len(test4) != 24) return 4;
    
    // Test string equality
    char *str1 = "ABC";
    char *str2 = "ABC";
    // Note: These may be different addresses
    // but should have same content
    if (str1[0] != 'A' || str1[1] != 'B' || str1[2] != 'C') return 5;
    if (str2[0] != 'A' || str2[1] != 'B' || str2[2] != 'C') return 6;
    
    char *str3 = "XYZ";
    if (str3[0] == 'A') return 7;  // Should be different
    
    return 42;
}

// test_strings
[[cccc::test(return = 42)]]
int test_strings(void) {
    char *str1 = "Hello";
    char *str2 = "World";
    
    // Test string comparison and access
    if (str1[0] == 'H' && str1[4] == 'o') {
        if (str2[0] == 'W' && str2[4] == 'd') {
            return 42;
        }
    }
    
    return 0;
}

// test_strings_and_arrays
[[cccc::test(return = 42)]]
int test_strings_and_arrays(void) {
    // Array of string pointers
    char *strings[3];
    strings[0] = "First";
    strings[1] = "Second";
    strings[2] = "Third";
    
    // Test accessing first character of each string
    if (strings[0][0] != 'F') return 1;
    if (strings[1][0] != 'S') return 2;
    if (strings[2][0] != 'T') return 3;
    
    // Test accessing other characters
    if (strings[0][4] != 't') return 4;  // "First"[4] = 't'
    if (strings[1][5] != 'd') return 5;  // "Second"[5] = 'd'
    if (strings[2][4] != 'd') return 6;  // "Third"[4] = 'd'
    
    // Character array
    char chars[5];
    chars[0] = 'A';
    chars[1] = 'B';
    chars[2] = 'C';
    chars[3] = 'D';
    chars[4] = '\0';
    
    // Test character array
    if (chars[0] != 'A') return 7;
    if (chars[1] != 'B') return 8;
    if (chars[2] != 'C') return 9;
    if (chars[3] != 'D') return 10;
    if (chars[4] != '\0') return 11;
    
    // Test pointer arithmetic with string from array
    char *ptr = strings[1];  // "Second"
    char *ptr2 = ptr + 3;    // "ond"
    if (ptr2[0] != 'o') return 12;
    if (ptr2[1] != 'n') return 13;
    if (ptr2[2] != 'd') return 14;
    
    return 42;
}

// test_strings_comprehensive
[[cccc::test(return = 42)]]
int test_strings_comprehensive(void) {
    // Test 1: Basic string literal assignment
    char *str1 = "Hello";
    if (str1[0] != 'H') return 1;
    if (str1[1] != 'e') return 2;
    if (str1[2] != 'l') return 3;
    if (str1[3] != 'l') return 4;
    if (str1[4] != 'o') return 5;
    if (str1[5] != '\0') return 6;
    
    // Test 2: Multiple string literals
    char *str2 = "World";
    if (str2[0] != 'W') return 7;
    if (str2[4] != 'd') return 8;
    
    // Test 3: Empty string
    char *empty = "";
    if (empty[0] != '\0') return 9;
    
    // Test 4: String with special characters
    char *special = "A\nB\tC";
    if (special[0] != 'A') return 10;
    if (special[1] != '\n') return 11;
    if (special[2] != 'B') return 12;
    if (special[3] != '\t') return 13;
    if (special[4] != 'C') return 14;
    
    // Test 5: String pointers can be compared
    char *dup1 = "Test";
    char *dup2 = "Test";
    // Note: In this implementation, same string literals 
    // may not be deduplicated, so we just test access
    if (dup1[0] != 'T') return 15;
    if (dup2[0] != 'T') return 16;
    
    // Test 6: Pointer arithmetic with strings
    char *ptr = "ABCDEF";
    char *ptr2 = ptr + 2;
    if (ptr2[0] != 'C') return 17;
    if (ptr2[1] != 'D') return 18;
    
    // Test 7: Numeric string
    char *nums = "0123456789";
    if (nums[0] != '0') return 19;
    if (nums[9] != '9') return 20;
    
    // All tests passed!
    return 42;
}

// test_unicode_comments
[[cccc::test(return = 42)]]
int test_unicode_comments(void) {
    // Unicode in string literals
    const char *em_dash  = "em \xe2\x80\x94 dash";
    const char *section  = "\xc2\xa7 7.28";

    if (strlen(em_dash) != 11) return 1;
    if (strlen(section) != 7)  return 2;

    return 42;
}

// test_wchar_uchar_headers
[[cccc::test(return = 42)]]
int test_wchar_uchar_headers(void) {
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

// [from test_format_string_valid]
// Format string validation: valid printf/sprintf calls with --format-string-checks.
[[cccc::test(return = 42, flags = "--format-string-checks")]]
int test_format_string_valid(void) {
    char buf[100];
    printf("Number: %d\n", 42);
    printf("Mixed: %d %s %f\n", 42, "hello", 3.14);
    printf("Percentage: 100%%\n");
    printf("Formatted: %10d\n", 42);
    sprintf(buf, "x=%d y=%d", 10, 20);
    printf("Result: %s\n", buf);
    return 42;
}

// [from test_format_string_complex]
// Format string validation: * width/precision specifiers, length modifiers.
[[cccc::test(return = 42, flags = "--format-string-checks")]]
int test_format_string_complex(void) {
    printf("Width from arg: %*d\n", 10, 42);
    printf("Precision from arg: %.*f\n", 2, 3.14159);
    printf("Both dynamic: %*.*f\n", 10, 2, 3.14159);
    printf("Long: %ld\n", 42L);
    printf("Long long: %lld\n", 42LL);
    return 42;
}

// [from test_format_length_modifier_valid]
// Format string validation: length modifiers with correct types.
[[cccc::test(return = 42, flags = "--format-string-checks")]]
int test_format_length_modifier_valid(void) {
    long l = 42L;
    unsigned long ul = 100UL;
    long double ld = 3.14L;
    printf("%ld\n",  l);
    printf("%lu\n",  ul);
    printf("%Lf\n",  ld);
    printf("%zu\n",  ul);
    return 42;
}

#pragma cccc suite end
