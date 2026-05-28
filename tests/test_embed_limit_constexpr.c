// Test #embed limit parameter with full constant expressions
#define LIMIT_BASE 1
#define LIMIT_EXPR (LIMIT_BASE + 1)
#define LIMIT_MAX(a, b) ((a) > (b) ? (a) : (b))

int main() {
    unsigned char arithmetic[] = {
        #embed "embed_data/test_data.bin" limit(1 + 1)
    };

    unsigned char macro[] = {
        #embed "embed_data/test_data.bin" limit(LIMIT_EXPR)
    };

    unsigned char conditional[] = {
        #embed "embed_data/test_data.bin" limit(0 ? 1 : 2)
    };

    unsigned char function_macro[] = {
        #embed "embed_data/test_data.bin" limit(LIMIT_MAX(1, 2))
    };

    unsigned char cast_sizeof[] = {
        #embed "embed_data/test_data.bin" limit((int)sizeof(char[2]))
    };

    if (sizeof(arithmetic) != 2 || sizeof(macro) != 2 ||
        sizeof(conditional) != 2 || sizeof(function_macro) != 2 ||
        sizeof(cast_sizeof) != 2) {
        return 1;
    }

    if (arithmetic[0] != 10 || macro[1] != 20 || conditional[0] != 10 ||
        function_macro[1] != 20 || cast_sizeof[1] != 20) {
        return 2;
    }

    return 42;
}
