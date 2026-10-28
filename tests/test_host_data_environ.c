// An extern the program never defines binds to the host's own object.
extern char **environ;

int main(void) {
    return environ && environ[0] ? 42 : 1;
}
