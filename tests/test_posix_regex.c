#include <regex.h>

int main(void) {
    regex_t re;
    regmatch_t match[2];

    int rc = regcomp(&re, "j([0-9]+)", REG_EXTENDED);
    if (rc != 0) return 1;
    if (re.re_nsub != 1) return 2;

    rc = regexec(&re, "j42", 2, match, 0);
    if (rc != 0) return 3;
    if (match[0].rm_so != 0 || match[0].rm_eo != 3) return 4;
    if (match[1].rm_so != 1 || match[1].rm_eo != 3) return 5;

    rc = regexec(&re, "abc", 0, 0, 0);
    if (rc != REG_NOMATCH) return 6;

    regfree(&re);
    return 42;
}
