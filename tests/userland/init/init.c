/*
 * NeolyxOS Init Process
 * 
 * First userland process (PID 1).
 * Spawns the shell and reaps orphaned processes.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/syscall.h"

/* Shell path */
#define SHELL_PATH "/bin/nxsh"

void _start(void) {
    println("[init] NeolyxOS Init Process starting...");
    
    /* Spawn shell */
    int pid = fork();
    
    if (pid < 0) {
        println("[init] ERROR: fork() failed!");
        exit(1);
    }
    
    if (pid == 0) {
        /* Child: exec shell */
        char *argv[] = { SHELL_PATH, 0 };
        exec(SHELL_PATH, argv, 0);
        
        /* If exec fails */
        println("[init] ERROR: Failed to exec shell!");
        exit(1);
    }
    
    /* Parent: init process */
    println("[init] Shell spawned.");
    
    /* Reap zombies forever */
    while (1) {
        int status;
        int child = wait(-1, &status);
        
        if (child > 0) {
            /* A child exited - could log it */
        }
        
        /* Small delay to avoid busy-waiting */
        yield();
    }
}
