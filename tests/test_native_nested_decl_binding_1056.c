// A block-scope declaration of an external function binds to it under the VM
// and -c=native.
int helper(int x) {
    return x + 1;
}
int main(void) {
    int helper(int); // redundant block-scope prototype
    return helper(41);
}
