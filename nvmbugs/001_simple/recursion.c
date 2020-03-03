#include <stdlib.h>

int mult(int a, int b) { return a * b; }

int factorial(int a) {
    if (a <= 1) return a;
    return a * factorial(a - 1);
}

int main(int argc, char **argv) {

    if (argc < 4) {
        return -1;
    }

    int op = atoi(argv[1]);
    int a = atoi(argv[2]);
    int b = atoi(argv[3]);

    if (op == 1) {
        return factorial(a);
    } else {
        return mult(a, b);
    }

    return -1;
}
