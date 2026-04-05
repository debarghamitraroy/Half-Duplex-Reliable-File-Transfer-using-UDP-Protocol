#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

void *other_thread(void *args) {
    for (int i = 1; i <= 10; i++) {
        printf("Other thread excuted %d times\n", i);
        sleep(1);
    }
    return NULL;
}

void *number_printer(void *args) {
    for (int i = 0; i < 10; i++) {
        printf("%d\n", i);
        if (i == 5) {
            pthread_t other;
            pthread_create(&other, NULL, other_thread, NULL);
            pthread_join(other, NULL);
        }
        usleep(2000000);
    }
    return NULL;
}

void *letter_printer(void *args) {
    for (char ch = 'A'; ch <= 'Z'; ch++) {
        printf("%c\n", ch);
        usleep(1500000);
    }
    return NULL;
}

int main() {
    pthread_t number_thread, letter_thread;
    printf("Before Thread\n");
    pthread_create(&number_thread, NULL, number_printer, NULL);
    pthread_create(&letter_thread, NULL, letter_printer, NULL);
    pthread_join(number_thread, NULL);
    printf("Reaching the end of number thread\n");
    pthread_join(letter_thread, NULL);
    printf("Reaching the end of letter thread\n");
    printf("After Thread\n");
    exit(0);
}