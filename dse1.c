int test1(int *p) {
    *p = 1;      // should be dead
    *p = 2;      // last store before use
    return *p;
}

