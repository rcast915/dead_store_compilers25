// file: branch_example.c
void branch_example(int *p, int *q, int cond) {
  *p = 1;           // store 1
  if (cond)
    *p = 2;         // store 2 (branch A)
  else
    *p = 3;         // store 3 (branch B)
  *q = *p;          // load and store to q
}

