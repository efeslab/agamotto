#include <stdio.h>
#include <stdlib.h>

#define ENV_VAR "TEST_ENV_VAR"

void __attribute__((constructor)) ctor(void) {
    printf("%s=%s\n", ENV_VAR, getenv(ENV_VAR));
}

int main(int argc, const char *argv[]) {
    return 0;
}