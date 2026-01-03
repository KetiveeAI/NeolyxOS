/*
 * NeolyxOS Userland Shell (nxsh)
 * 
 * Minimal POSIX-like shell running in Ring 3.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/syscall.h"

#define MAX_CMD_LEN 256
#define MAX_ARGS    16

/* Input buffer */
static char input[MAX_CMD_LEN];
static int input_pos = 0;

/* Current working directory */
static char cwd[256] = "/";

/* Read a line from stdin */
static int readline(char *buf, int max) {
    int i = 0;
    char c;
    
    while (i < max - 1) {
        if (read(STDIN_FD, &c, 1) <= 0) {
            continue;
        }
        
        if (c == '\n' || c == '\r') {
            buf[i] = '\0';
            print("\n");
            return i;
        } else if (c == 127 || c == '\b') {
            /* Backspace */
            if (i > 0) {
                i--;
                print("\b \b");
            }
        } else if (c >= 32 && c < 127) {
            buf[i++] = c;
            write(STDOUT_FD, &c, 1);
        }
    }
    buf[i] = '\0';
    return i;
}

/* Parse command into arguments */
static int parse_args(char *cmd, char **argv) {
    int argc = 0;
    char *p = cmd;
    
    while (*p && argc < MAX_ARGS - 1) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        
        argv[argc++] = p;
        
        /* Find end of argument */
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = 0;
    return argc;
}

/* String comparison */
static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (*a == '\0' && *b == '\0');
}

/* Built-in: cd */
static void cmd_cd(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/";
    if (chdir(path) < 0) {
        print("cd: no such directory\n");
    } else {
        getcwd(cwd, sizeof(cwd));
    }
}

/* Built-in: pwd */
static void cmd_pwd(void) {
    println(cwd);
}

/* Built-in: help */
static void cmd_help(void) {
    println("NeolyxOS Shell (nxsh)");
    println("Built-in commands:");
    println("  cd <dir>   - Change directory");
    println("  pwd        - Print working directory");
    println("  exit       - Exit shell");
    println("  help       - Show this help");
    println("");
    println("Run programs: /bin/hello");
}

/* Execute external command */
static void exec_cmd(int argc, char **argv) {
    if (argc == 0) return;
    
    int pid = fork();
    if (pid < 0) {
        print("fork failed\n");
        return;
    }
    
    if (pid == 0) {
        /* Child process */
        exec(argv[0], argv, 0);
        /* If exec returns, command not found */
        print(argv[0]);
        print(": command not found\n");
        exit(1);
    } else {
        /* Parent: wait for child */
        int status;
        wait(pid, &status);
    }
}

/* Main shell loop */
void _start(void) {
    char *argv[MAX_ARGS];
    int argc;
    
    println("NeolyxOS Shell v1.0");
    println("Type 'help' for commands.\n");
    
    /* Get initial cwd */
    getcwd(cwd, sizeof(cwd));
    
    while (1) {
        /* Print prompt */
        print("nxsh:");
        print(cwd);
        print("> ");
        
        /* Read command */
        if (readline(input, MAX_CMD_LEN) == 0) {
            continue;
        }
        
        /* Parse arguments */
        argc = parse_args(input, argv);
        if (argc == 0) continue;
        
        /* Handle built-in commands */
        if (streq(argv[0], "exit")) {
            println("Goodbye!");
            exit(0);
        } else if (streq(argv[0], "cd")) {
            cmd_cd(argc, argv);
        } else if (streq(argv[0], "pwd")) {
            cmd_pwd();
        } else if (streq(argv[0], "help")) {
            cmd_help();
        } else {
            /* External command */
            exec_cmd(argc, argv);
        }
    }
}
