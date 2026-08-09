// EXPECT_COMPILE_ERROR
// Test multiple statement errors with valid code interleaved

int func1() {
    int a = 1;
    break;  // Error 1
    int b = 2;
    return a + b;
}

int func2() {
    int x = 10;
    continue;  // Error 2
    int y = 20;
    return x + y;
}

int func3() {
    case 100:  // Error 3
    int z = 5;
    return z;
}

int main() {
    default:  // Error 4
    int result = func1() + func2() + func3();

    break;  // Error 5
    continue;  // Error 6

    return result;
}
