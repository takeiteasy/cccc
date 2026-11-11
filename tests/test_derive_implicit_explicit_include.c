// An explicit include alongside the implicit load is a no-op.
#include <cccc/derive.h>
#include <cccc/json.h>

@to_json @equals typedef struct {
    int a;
} P;

int main(void) {
    P p = {1};
    return P_equals(&p, &p) ? 42 : 1;
}
