void array_loop(int *A, int *B, int N) {
    for (int i = 0; i < N; i++) {
        int t = A[i];          // load
        B[i] = t + 1;          // store
        A[i] = B[i] * 2;       // another store
    }
}

