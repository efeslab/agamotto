#include <stdio.h>
#include <string.h>

int main(int argc, const char *argv[]) {
    char arr[1024];
    for (int i = 0; i < argc; ++i) {
        printf("%p %p\n", arr, &arr[0]);
        memcpy(&arr[0], argv[i], strlen(argv[i]));
        printf("%s\n", arr);
    }

    return 0;
}