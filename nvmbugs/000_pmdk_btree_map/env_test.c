#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int main(int argc, const char *argv[]) {
    const char *thing1 = getenv("TEST_ENV_VAR");
    const char *thing2 = secure_getenv("TEST_ENV_VAR");
    if (thing1 && thing2) {
        printf("1) TEST_ENV_VAR='%s'\n", thing1);
        printf("\tLen: %lu\n", strlen(thing1));

        printf("2) TEST_ENV_VAR='%s'\n", thing2);
        printf("\tLen: %lu\n", strlen(thing2));
    } else {
        printf("Error: TEST_ENV_VAR is NULL\n");
        return -1;
    }

    return 0;
}