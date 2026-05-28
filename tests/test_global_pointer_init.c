int global_value = 40;
int *global_ptr = &global_value;

int read_global(void) {
    return *global_ptr + 1;
}

int (*global_fn_ptr)(void) = read_global;

int main(void) {
    if (*global_ptr != 40)
        return 1;
    global_value = 41;
    if (global_fn_ptr() != 42)
        return 2;
    return 42;
}
