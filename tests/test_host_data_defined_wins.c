// CCCC_NATIVE_SKIP: the definition would collide with libc's environ
// A definition in the program wins over the host object of the same name.
extern char **environ;
char        **environ = 0;

int main(void) {
    return environ == 0 ? 42 : 1;
}
