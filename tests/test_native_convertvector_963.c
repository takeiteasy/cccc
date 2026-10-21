// -c=native, __builtin_convertvector round-trips with an attributed vector type
// name
typedef int   v4i __attribute__((vector_size(16)));
typedef float v4f __attribute__((vector_size(16)));
int main(void) {
    v4i a = {10, 11, 10, 11};
    v4f b = __builtin_convertvector(a, v4f);
    return (int)(b[0] + b[1] + b[2] + b[3]);
}
