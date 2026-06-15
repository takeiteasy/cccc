// #pragma cccc config(optimisation = 1) enables -O1 (constant folding)
// with no -O flag on the CLI, without changing program behaviour.

#pragma cccc config(optimisation = 1)

int main(void) {
    int a = 40;
    int b = 2;
    int c = a + b; // foldable at compile time when -O1 is active
    return c;
}
