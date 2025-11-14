void test1(int *p) {
    *p = 1;           // store A — writes 1
    int x = *p;       // use — reads the value 1
    *p = 2;           // store B — overwrites it
    (void)x;
}

