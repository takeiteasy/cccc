// A local variable shadowing a global round-trips under -c=native.
#include <string.h>
typedef struct {
    int a;
} LocalShadows1302Widget;
int local_shadows_1302_helper(int x) {
    return x + 1;
}
int local_shadows_1302_g = 7;
int local_shadows_1302_use_fn(int flag, int a) {
    if (flag) {
        int local_shadows_1302_helper = a * 2;
        return local_shadows_1302_helper;
    }
    return local_shadows_1302_helper(a);
}
int local_shadows_1302_use_var(int flag, int a) {
    if (flag) {
        int local_shadows_1302_g = a * 2;
        a                        = local_shadows_1302_g;
    }
    return a + local_shadows_1302_g;
}
int local_shadows_1302_use_ffi(int flag, const char *s) {
    if (flag) {
        int strlen = 3;
        return strlen;
    }
    return (int)strlen(s);
}
int local_shadows_1302_use_typedef(int flag) {
    if (flag) {
        int LocalShadows1302Widget = 5;
        return LocalShadows1302Widget;
    }
    LocalShadows1302Widget w;
    w.a = 42;
    return w.a;
}
int main(void) {
    if (local_shadows_1302_use_fn(0, 41) != 42)
        return 1;
    if (local_shadows_1302_use_var(0, 42) != 49)
        return 1;
    if (local_shadows_1302_use_ffi(0, "ab") != 2)
        return 1;
    if (local_shadows_1302_use_typedef(0) != 42)
        return 1;
    return 42;
}
