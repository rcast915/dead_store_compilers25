// q aliases p but is a different pointer operand
// our mustAlias is "same operand" -> not must-alias -> keep
int f(int *p, int *q) {
  *p = 1;     // kept
  *q = 2;     // kept (even if q==p at runtime)
  return *p;
}

