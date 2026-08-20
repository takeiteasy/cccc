// CCCC_FLAGS: -Wall -Wno-unused -Wno-shadow -Wno-deprecated --std=c23
// CCCC_REJECT_STDERR: warning:

int value;
int [[deprecated]] old_value = 1;

int check(int unused_parameter) {
    int value = 41;
unused_label:;
    return value + old_value;
}

int main(void) {
    return check(0);
}
