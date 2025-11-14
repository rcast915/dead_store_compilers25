// store in "then", later store in a block that does NOT post-dom the first
// our pass requires post-dominance; expect no deletion
int f(int *p, int c) {
  if (c) {
    *p = 1;     // kept
    return *p;
  } else {
    *p = 2;     // kept
    return *p;
  }
}

