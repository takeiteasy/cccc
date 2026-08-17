// CCCC_FLAGS: -Wredundant-decls
// CCCC_REJECT_STDERR: warning:

// #1043: a declaration following a K&R-form *definition* must not warn --
// the definition already set is_definition, same as the prototype form.
int knr(a, b)
int a;
int b;
{ return a + b; }

int knr(int, int);

int main(void) { return knr(40, 2); }
