// A scalar compared with a vector is splatted; the result is a -1/0 mask.
typedef float f4 __attribute__((vector_size(16)));
typedef int i4 __attribute__((vector_size(16)));
typedef float e4 __attribute__((ext_vector_type(4)));
int main(void){
  f4 a={1,2,3,4}; i4 m=a>2; i4 n=2<a; i4 q=a==2.0f; i4 r=a<=2;
  e4 b={1,2,3,4}; i4 t=b!=3;
  if(m[0]||m[1]||!m[2]||!m[3]) return 1;
  if(n[0]||n[1]||!n[2]) return 2;
  if(q[0]||!q[1]||q[2]) return 3;
  if(!r[0]||!r[1]||r[2]) return 4;
  if(!t[0]||!t[1]||t[2]||!t[3]) return 5;
  return 42;}
