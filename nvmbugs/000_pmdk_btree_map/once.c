#include <pthread.h>
#include <stdio.h>

void init(void) { printf("hello!\n"); }

pthread_once_t once = PTHREAD_ONCE_INIT;

int main(void) {
    for (int i = 0; i < 100; ++i) pthread_once(&once, init);
}