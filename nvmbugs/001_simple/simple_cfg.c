#include <stdlib.h>


int mult(int a, int b) {
    if (a < 0) {
        return -1;
    }
    if (b < 0) {
        return -1;
    }
    return a * b;
}

int sum(int a, int b) {
    if (a < 0) {
        return -1;
    }
    if (b < 0) {
        return -1;
    }
    return a + b;
}

int sum_exit(int a, int b) {
    if (a < 0) {
        exit(-1);
    }
    if (b < 0) {
        exit(-1);
    }
    return a + b;
}

int mult_loop(int a, int b) {
    int res = 0;
    if (a < 0) {
        return -1;
    }
    if (b < 0) {
        return -1;
    }
    for (int i = 0; i < b; i++) {
        res += a;
    }
    return res;
}

int complex_math(int a, int b) {
    int c;
    if (a < 0) goto err;
    if (b < 0) goto err;

    if (a) c = mult_loop(a, b);
    else c = sum(a, b);

    return c;
err:
    return -1;
}

int main(int argc, char **argv) {

    if (argc < 4) {
        return -1;
    }

    int op = atoi(argv[1]);
    int a = atoi(argv[2]);
    int b = atoi(argv[3]);

    (void)complex_math(a, b);

    if (op == 1) {
        return mult(a, b);
    } else if (op == 2) {
        return mult_loop(a, b);
    } else if (op < 0) {
        return sum_exit(a, b);
    } else {
        return sum(a, b);
    }

    return -1;
}
