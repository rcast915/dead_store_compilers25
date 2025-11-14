// two stores to same ptr, no use in between
int f(int *p) {
  *p = 1;      // dead
  *p = 2;      // kept
  return *p;
}

