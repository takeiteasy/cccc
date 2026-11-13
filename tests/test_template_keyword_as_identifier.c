// `__template` is a template keyword only before `(`; glibc's
// `mkstemp (char *__template)` names a parameter with it.
int use(char *__template) { return __template[0]; }
int use2(int __specialize, int __template) { return __specialize + __template; }

@template(T) T twice(T a) { return a + a; }
__template(T) T thrice(T a) { return a + a + a; }

int main(void) {
    char s[] = {40, 0};
    return use(s) + use2(1, 0) + @inst(int) twice(0) + thrice@(int)(0) + 1;
}
