#include <cccc/derive.h>
#include <cccc/text.h>
#include <stdio.h>

@to_string @from_string struct Ref {
    int  x;
    int *p @to_string_skip;
};

int main(void) {
    struct Ref r = {0};
    if (Ref_from_string(&r, "Ref { x = 42 }"))
        return 1;
    return r.p == NULL ? r.x : 2;
}
