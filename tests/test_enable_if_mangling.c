// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: _Z4signUa9enable_ifIXgtfL0p_Li0EEEi
// CCCC_EXPECT_STDOUT: _Z4signi\(
// CCCC_EXPECT_STDOUT: _Z4pickUa9enable_ifIXeqplfL0p_fL0p0_Li3EEXntfL0p0_EEii
// CCCC_EXPECT_STDOUT: _Z4fromUa9enable_ifIXltcvifL0p_ngLi2EEEl
// CCCC_EXPECT_STDOUT: _Z4fromUa9enable_ifIXaanefL0p_Li0ELj1EEEPc
// CCCC_EXPECT_STDOUT: _Z5scaleUa9enable_ifIXaaclL_Z20__builtin_constant_pEfL0p_EgtfL0p_Li0EEEi
// CCCC_EXPECT_STDOUT: _Z4fillUa9enable_ifIXlefL0p0_clL_Z21__builtin_object_sizeEfL0pK_Li1EEEEPKcU17pass_object_size1i
// CCCC_EXPECT_STDOUT: _Z4sizePKvU17pass_object_size0
// CCCC_EXPECT_STDOUT: _Z4sizePKcU25pass_dynamic_object_size1i
// CCCC_EXPECT_STDOUT: _Z4pairPKcU17pass_object_size0S0_U17pass_object_size0
// CCCC_EXPECT_STDOUT: _Z3oneUa9enable_ifILi1EEi
//
// enable_if and pass_object_size overloads get the names clang gives them.
#include <stddef.h>

#define OVL      __attribute__((overloadable))
#define PS(type) __attribute__((pass_object_size(type)))

// clang-format off
__attribute__((overloadable))
__attribute__((enable_if(a > 0, "")))
int sign(int a) {
    return 1;
}

int OVL sign(int a) {
    return 2;
}

__attribute__((overloadable))
__attribute__((enable_if(a + b == 3, "")))
__attribute__((enable_if(!b, "")))
int pick(int a, int b) {
    return 1;
}

__attribute__((overloadable))
__attribute__((enable_if((int)a < -2, "")))
int from(long a) {
    return 1;
}

__attribute__((overloadable))
__attribute__((enable_if(p != 0 && 1u, "")))
int from(char *p) {
    return 1;
}

__attribute__((overloadable))
__attribute__((enable_if(__builtin_constant_p(a) && a > 0, "")))
int scale(int a) {
    return 1;
}

__attribute__((overloadable))
__attribute__((enable_if(n <= __builtin_object_size(p, 1), "")))
int fill(const char *const p PS(1), int n) {
    return 1;
}

__attribute__((overloadable))
__attribute__((enable_if(1, "")))
int one(int a) {
    return 1;
}
// clang-format on

int OVL size(const void *const p PS(0)) {
    return 1;
}

int OVL size(const char *const p __attribute__((pass_dynamic_object_size(1))),
             int               x) {
    return 1;
}

int OVL pair(const char *const p PS(0), const char *const q PS(0)) {
    return 1;
}

int main(void) {
    return 42;
}
