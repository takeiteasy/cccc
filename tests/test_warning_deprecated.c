// CCCC_FLAGS: -Wdeprecated --std=c23
// CCCC_EXPECT_STDERR: 5 warnings generated.

int old_function(void) __attribute__((deprecated("use replacement")));
int old_function(void) {
    return 1;
}

int [[deprecated]] old_variable = 2;
typedef int OldInt [[deprecated("use int")]];
enum Values { OLD_VALUE [[deprecated]] = 3 };
struct [[deprecated("use another struct")]] OldStruct { int value; };

int main(void) {
    OldInt value = old_variable + OLD_VALUE;
    struct OldStruct old = {36};
    return old_function() + value + old.value;
}
