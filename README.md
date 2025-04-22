# User-Level Thread Management Library

This project implements a user-level threading system in C with:

- Preemptive Round-Robin scheduling (100ms quantum)
- Context switching using `sigsetjmp` / `siglongjmp`
- Signal-driven thread preemption via `SIGVTALRM` and `setitimer`
- Thread lifecycle: create, block, sleep, unblock, terminate

## Compilation

```bash
gcc -Wall -g uthreads.c main.c -o uthreads_test
```

```bash
./uthreads.test
```
## Purpose
This project simulates how operating systems manage thread scheduling, providing an understanding of context switching and signal safety.
