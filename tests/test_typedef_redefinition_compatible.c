// A same-scope typedef redeclaration with a compatible type is accepted.
typedef int      T;
typedef int      T;
typedef struct S S;
typedef struct S S;
struct S {
    int a;
};
typedef struct S S;
enum E { X = 40 };
typedef enum E E;
typedef enum E E;
typedef int    A[3];
typedef int    A[3];
typedef int (*F)(void);
typedef int (*F)(void);

int main(void) {
    S s = {2};
    T t = s.a + X;
    return t;
}
