// unknown call may read memory -> conservatively keep
extern void g(void);
int f(int *p) {
  *p = 1;     // kept
  g();        // intervening mem op
  *p = 2;     // kept
  return *p;
}

