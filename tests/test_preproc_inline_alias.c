/* Test that __inline and __inline__ are recognised as spelling aliases for
   inline, matching GCC's predefined keyword aliases.  Both spellings must
   compile without error and the program must exit 42. */
extern __inline int add1(int x) { return x + 1; }
static __inline__ int add2(int x) { return x + 2; }
int main(void) { return add1(add2(39)); } /* 39 + 2 + 1 = 42 */
