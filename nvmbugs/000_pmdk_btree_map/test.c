#include <string.h>

#define SZ 1024

int main(int argc, const char *argv[]) {
    char a[SZ];
    char b[SZ];

    memset(a, 0, SZ);
    memset(b, 1, SZ);

    memcpy(a, b, SZ);   

    return 0;
}