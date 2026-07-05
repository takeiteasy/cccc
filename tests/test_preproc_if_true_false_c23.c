// C23 §6.10.1: true→1 and false→0 in #if constant-expression context.
// In C23 mode true/false are TK_KEYWORD and must be mapped to 1/0 before
// the preprocessor evaluates the expression (not left as unhandled keywords).
#if !true
#error true must be truthy in #if
#endif
#if true != 1
#error true must equal 1 in #if
#endif
#if false != 0
#error false must equal 0 in #if
#endif
#if true && !false
int main(void) { return 42; }
#else
#error true/false combined logic wrong
#endif
