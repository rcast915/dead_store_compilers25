// first store, then branch, then overwrite after join -> later store post-dominates
int f(int *p, int c) {
  *p = 1;       // dead
  if (c) {      // no memory use here
  } else {      // no memory use here
  }
  *p = 2;       // kept (post-dominates the first)
  return *p;
}

