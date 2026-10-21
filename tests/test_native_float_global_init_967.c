// -c=native, a float global initializer whose value prints with no decimal
// point under %.9g (e.g. 1.0f) no longer emits the invalid token `1f` --
// serialize_init_bytes's TY_FLOAT arm now forces a decimal point before
// appending the f suffix

float g = 1.0f;
float h = -2.0f;
float z = 0.0f;

int main(void) {
    if (g != 1.0f)
        return 1;
    if (h != -2.0f)
        return 2;
    if (z != 0.0f)
        return 3;
    return 42;
}
