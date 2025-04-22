
#include <stdio.h>
#include "uthreads.h"

int finished[4] = {0};
int thread2_has_blocked = 0;

void thread1() {
    printf("Thread 1: Starting and will sleep for 3 quantums.\n");
    uthread_sleep_quantums(3);
    printf("Thread 1: Woke up and exiting.\n");
    finished[1] = 1;
    uthread_exit(1);
}

void thread2() {
    printf("Thread 2: Starting and will block itself now.\n");
    thread2_has_blocked = 1;
    uthread_block(2);
    printf("Thread 2: Unblocked and exiting.\n");
    finished[2] = 1;
    uthread_exit(2);
}

void thread3() {
    printf("Thread 3: Starting.\n");
    for (int i = 0; i < 3; ++i) {
        printf("Thread 3: Iteration %d.\n", i + 1);
        for (volatile int j = 0; j < 1000000; ++j); 
    }
    printf("Thread 3: Exiting.\n");
    finished[3] = 1;
    uthread_exit(3);
}

int main() {
    if (uthread_system_init(100000) == -1) {
        printf("Error: Failed to initialize thread system.\n");
        return 1;
    }

    int tid1 = uthread_create(thread1);
    int tid2 = uthread_create(thread2);
    int tid3 = uthread_create(thread3);

    if (tid1 == -1 || tid2 == -1 || tid3 == -1) {
        printf("Error: Failed to create one or more threads.\n");
        return 1;
    }

    int rounds = 0;
    while (!(finished[1] && finished[2] && finished[3])) {
        if (thread2_has_blocked) {
            printf("Main: Unblocking Thread 2.\n");
            uthread_unblock(2);
            thread2_has_blocked = 0;
        }
        for (volatile int i = 0; i < 10000000; ++i); // Busy wait
        rounds++;
    }

    printf("Main: All threads finished. Exiting.\n");
    return 0;
}

/**
Expected output of this program :
Thread 1: Starting and will sleep for 3 quantums.
Thread 2: Starting and will block itself now.
Thread 3: Starting.
Thread 3: Iteration 1.
Thread 3: Iteration 2.
Thread 3: Iteration 3.
Thread 3: Exiting.
Main: Unblocking Thread 2.
Thread 2: Unblocked and exiting.
Thread 1: Woke up and exiting.
Main: All threads finished. Exiting.

**/