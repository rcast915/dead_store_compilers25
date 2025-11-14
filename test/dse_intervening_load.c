// load between stores -> prev store might be observed
int f(int *p) {
  *p = 1;          // kept
  int x = *p;      // intervening use
  *p = 2;          // kept
  return x + *p;
}

