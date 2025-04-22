#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "uthreads.h"

#ifdef __x86_64__
/* 64-bit architecture */
typedef unsigned long addr_t;
#define JMPBUF_SP 6
#define JMPBUF_PC 7
/* Address translation - use this as a black box */
addr_t translate_addr(addr_t addr)
{
 addr_t ret;
 asm volatile("xor %%fs:0x30,%0\n"
 "rol $0x11,%0\n"
 : "=g" (ret)
 : "0" (addr));
 return ret;
}
#else
/* 32-bit architecture */
typedef unsigned int addr_t;
#define JMPBUF_SP 4
#define JMPBUF_PC 5
addr_t translate_addr(addr_t addr)
{
 addr_t ret;
 asm volatile("xor %%gs:0x18,%0\n"
 "rol $0x9,%0\n"
 : "=g" (ret)
 : "0" (addr));
 return ret;
}
#endif

// possible states of a thread
typedef enum { READY, RUNNING, BLOCKED } thread_state;

/*******************
	Thread Srtuct
********************/
typedef struct {
    int tid;
    bool is_initialized; // for safe access to threads array fields
    thread_state state;
    sigjmp_buf context;
    char stack[UTHREAD_STACK_BYTES];
    int sleep_quantums_remaining;
}thread_t;

/****************************
	Queue Struct & Methods
*****************************/

typedef struct {
    int queue[UTHREAD_MAX_THREADS];
    int front;
    int back;
    int size;
} queue_t;

int enqueue(queue_t* q, int tid) {
    if (q->size == UTHREAD_MAX_THREADS) return -1;
    q->queue[q->back] = tid;
    q->back = (q->back + 1) % UTHREAD_MAX_THREADS;
    q->size++;
    return 0;
}

int dequeue(queue_t* q) {
    if (q->size == 0) return -1;
    int tid = q->queue[q->front];
    q->front = (q->front + 1) % UTHREAD_MAX_THREADS;
    q->size--;
    return tid;
}

int is_empty(queue_t* q) {
    return q->size == 0;
}

/***********************
	Global Variables		
************************/

static thread_t threads[UTHREAD_MAX_THREADS];
static int current_tid = 0;
static int global_quantum = 0;
static struct itimerval timer; //for timer management
static sigset_t signal_set; // used for temporarily block SIGVTALRM  when we execute critical code
static queue_t ready_queue;


void scheduler_handler(int sig)
{
    sigprocmask(SIG_BLOCK, &signal_set, NULL);
	
    int ret = sigsetjmp(threads[current_tid].context, 1);
    if (ret != 0) {
        sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
        return;
    }
	
	//handling sleeping threads
    for (int i = 0; i < UTHREAD_MAX_THREADS; ++i) {
        if (threads[i].is_initialized && threads[i].state == BLOCKED && threads[i].sleep_quantums_remaining > 0) {
            threads[i].sleep_quantums_remaining--;
            if (threads[i].sleep_quantums_remaining == 0) {
                threads[i].state = READY;
                enqueue(&ready_queue, i);
            }
        }
    }
	
    if (threads[current_tid].is_initialized && threads[current_tid].state == RUNNING) {
        threads[current_tid].state = READY;
        enqueue(&ready_queue, current_tid);
    }
	
    int next_tid = -1;
    while (!is_empty(&ready_queue)) {
        int candidate = dequeue(&ready_queue);
        if (threads[candidate].is_initialized && threads[candidate].state == READY) {
            next_tid = candidate;
            break;
        }
    }
    if (next_tid == -1) {
        if (threads[current_tid].state != BLOCKED) {
            threads[current_tid].state = RUNNING;
        }
        sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
        return;
    }
    current_tid = next_tid;
    threads[current_tid].state = RUNNING;
    sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
	
    siglongjmp(threads[current_tid].context, 1);
}

int uthread_system_init(int quantum_usecs) {
    if (quantum_usecs <= 0) return -1;
    global_quantum = quantum_usecs;
	
    ready_queue.front =0;
	ready_queue.back =0;
	ready_queue.size = 0;
	
    for (int i = 0; i < UTHREAD_MAX_THREADS; ++i) {
        threads[i].is_initialized = false;
        threads[i].sleep_quantums_remaining = 0;
    }
	
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGVTALRM);
	
	//initializing main thread
    threads[0].is_initialized = true;
    threads[0].tid = 0;
    threads[0].state = RUNNING;
    threads[0].sleep_quantums_remaining = 0;
    current_tid = 0;
	
	//dealing with future returning to the main thread (in context switching)*/
    if (sigsetjmp(threads[0].context, 1) != 0) {
        return 0;
    }
	
	//defining the action when a signal arrives 
    struct sigaction sa;
    sa.sa_handler = &scheduler_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0) {
        perror("sigaction");
        exit(1);
    }
	
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = global_quantum;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = global_quantum;
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL) < 0) {
        perror("setitimer");
        exit(1);
    }
    return 0;
}

int uthread_create(uthread_entry entry_func) {
    if (entry_func == NULL) return -1;
	
    sigprocmask(SIG_BLOCK, &signal_set, NULL);
    int tid = -1;
    for (int i = 0; i < UTHREAD_MAX_THREADS; i++) {
        if (!threads[i].is_initialized) {
            tid = i;
            break;
        }
    }
    if (tid == -1) {
        sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
        return -1;
    }
    memset(threads[tid].stack, 0, UTHREAD_STACK_BYTES);
	
	//initializing the thread
    threads[tid].tid = tid;
    threads[tid].is_initialized = true;
    threads[tid].state = READY;
    threads[tid].sleep_quantums_remaining = 0;
	
    addr_t sp = (addr_t)(threads[tid].stack + UTHREAD_STACK_BYTES - sizeof(addr_t));
    addr_t pc = (addr_t)(entry_func);
	
    sigsetjmp(threads[tid].context, 1);
    (threads[tid].context->__jmpbuf)[JMPBUF_SP] = translate_addr(sp);
    (threads[tid].context->__jmpbuf)[JMPBUF_PC] = translate_addr(pc);
    sigemptyset(&threads[tid].context->__saved_mask);
	
    enqueue(&ready_queue, tid);
    sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
	
    return tid;
}

int uthread_exit(int tid) {
    if (tid <= 0 || tid >= UTHREAD_MAX_THREADS || !threads[tid].is_initialized) {
        return -1;
    }
  
    sigprocmask(SIG_BLOCK, &signal_set, NULL);
	
    threads[tid].is_initialized = false;
    threads[tid].state = BLOCKED;
    threads[tid].sleep_quantums_remaining = 0;
	
    if (tid == current_tid) {
		
        int next_tid = -1;
        while (!is_empty(&ready_queue)) {
            int candidate = dequeue(&ready_queue);
            if (threads[candidate].is_initialized && threads[candidate].state == READY) {
                next_tid = candidate;
                break;
            }
        }
        if (next_tid == -1) {
            sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
            exit(0); // no more threads to run
        }
        current_tid = next_tid;
        threads[current_tid].state = RUNNING;
		
        sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
        siglongjmp(threads[current_tid].context, 1);
    }
    sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
    return 0;
}

int uthread_block(int tid) {
    if (tid <= 0 || tid >= UTHREAD_MAX_THREADS || !threads[tid].is_initialized) {
        return -1;
    }
    sigprocmask(SIG_BLOCK, &signal_set, NULL);
    if (threads[tid].state == BLOCKED) {
        sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
        return 0;
    }
    threads[tid].state = BLOCKED;
	
    if (tid == current_tid) {
        sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
        scheduler_handler(SIGVTALRM);
        return 0;
    }
    sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
    return 0;
}

int uthread_unblock(int tid) {
    sigprocmask(SIG_BLOCK, &signal_set, NULL);
    if (tid < 0 || tid >= UTHREAD_MAX_THREADS || !threads[tid].is_initialized) {
        sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
        return -1;
    }
    if (threads[tid].state != BLOCKED) {
        sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
        return 0;
    }
    threads[tid].state = READY;
    enqueue(&ready_queue, tid);
    sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
    return 0;
}

int uthread_sleep_quantums(int num_quantums) {
    if (num_quantums <= 0 || current_tid == 0) {
        return -1;
    }
    sigprocmask(SIG_BLOCK, &signal_set, NULL);
	
    threads[current_tid].sleep_quantums_remaining = num_quantums;
    threads[current_tid].state = BLOCKED;
    scheduler_handler(SIGVTALRM);
	
    sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
    return 0;
}
