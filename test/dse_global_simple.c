// simple global case, two stores, no intervening use
int g;
int f(void) {
  g = 1;      // dead
  g = 2;      // kept
  return g;
}

