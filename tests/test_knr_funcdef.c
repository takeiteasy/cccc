// K&R-style function definitions
int add(a, b)
int a;
int b;
{ return a + b; }

double scale(x, factor)
double x;
double factor;
{ return x * factor; }

int id(n)
{ return n; }

int main(void) {
    if (add(19, 23) != 42) return 1;
    if ((int)scale(3.0, 14.0) != 42) return 2;
    if (id(42) != 42) return 3;
    return 42;
}
