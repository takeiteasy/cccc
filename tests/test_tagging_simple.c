void *malloc(long size);
void free(void *ptr);

int main() {
    int *ptr = (int *)malloc(sizeof(int));
    *ptr = 42;
    int result = *ptr;
    free(ptr);
    return result;
}
