// Test: Unicode characters in comments and string literals
// em dash — en dash – section sign § bullet • copyright © trademark ™
// French: café crêpe naïve résumé
// Greek: αβγδ
// CJK: 日本語
/* Block comment: §7.28 — C23 standard references with Unicode */

#include <string.h>

int main(void) {
    // Unicode in string literals
    const char *em_dash  = "em \xe2\x80\x94 dash";
    const char *section  = "\xc2\xa7 7.28";

    if (strlen(em_dash) != 11) return 1;
    if (strlen(section) != 7)  return 2;

    return 42;
}
