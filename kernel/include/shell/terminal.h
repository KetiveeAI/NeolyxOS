/*
 * NeolyxOS Terminal Interface
 * 
 * Command Parser + Executor for NeolyxOS
 * Works in boot manager (kernel) and desktop (NXRender)
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_TERMINAL_H
#define NEOLYX_TERMINAL_H

#include <stdint.h>

/* ============ Status Codes ============ */

typedef enum {
    TERM_OK = 0,
    TERM_ERR_UNKNOWN_CMD,
    TERM_ERR_PERMISSION,
    TERM_ERR_INVALID_ARGS,
    TERM_ERR_NOT_FOUND,
    TERM_ERR_IO,
    TERM_EXIT
} term_status_t;

/* ============ Privilege Model ============ */

typedef enum {
    PRIV_USER = 0,      /* Normal user operations */
    PRIV_SYSTEM = 1     /* System-level operations (sys prefix) */
} term_privilege_t;

/* ============ Terminal State ============ */

#define TERM_INPUT_MAX      256
#define TERM_HISTORY_MAX    32
#define TERM_OUTPUT_MAX     4096
#define TERM_CWD_MAX        256
#define TERM_ARG_MAX        16
#define TERM_USERNAME_MAX   32

typedef struct {
    /* Current directory */
    char cwd[TERM_CWD_MAX];
    
    /* Current user */
    char username[TERM_USERNAME_MAX];
    
    /* Current privilege (reset after each command) */
    term_privilege_t privilege;
    
    /* Input buffer */
    char input[TERM_INPUT_MAX];
    int input_pos;
    
    /* Command history */
    char history[TERM_HISTORY_MAX][TERM_INPUT_MAX];
    int history_count;
    int history_pos;
    
    /* Output buffer */
    char output[TERM_OUTPUT_MAX];
    int output_len;
    
    /* Running state */
    int running;
    
} terminal_state_t;

/* ============ Command Definition ============ */

/* Command handler function type */
typedef term_status_t (*cmd_handler_t)(terminal_state_t *term, int argc, char **argv);

/* Command table entry */
typedef struct {
    const char *name;           /* Command name (e.g., "info", "sys") */
    const char *subcommand;     /* Subcommand (e.g., "system" for "info system", NULL if none) */
    int requires_sys;           /* 1 = requires sys prefix, 0 = user accessible */
    const char *description;    /* Help text */
    cmd_handler_t handler;      /* Function to execute */
} command_t;

/* ============ Terminal API ============ */

/* Initialize terminal state */
void terminal_init(terminal_state_t *term);

/* Process a single character input (for keyboard handling) */
void terminal_input_char(terminal_state_t *term, char c);

/* Process a scancode from keyboard */
void terminal_input_scancode(terminal_state_t *term, uint8_t scancode);

/* Execute current input as command */
term_status_t terminal_execute(terminal_state_t *term);

/* Parse input into argc/argv */
int terminal_parse(const char *input, char **argv, int max_args);

/* Look up command in dispatch table */
const command_t* terminal_lookup(const char *cmd, const char *subcmd);

/* Print to terminal output */
void terminal_print(terminal_state_t *term, const char *str);
void terminal_println(terminal_state_t *term, const char *str);

/* Clear terminal output */
void terminal_clear_output(terminal_state_t *term);

/* Get prompt string (includes cwd) */
const char* terminal_get_prompt(terminal_state_t *term);

/* ============ Built-in Command Handlers ============ */

/* Shell utilities */
term_status_t cmd_help(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_clear(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_exit(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_version(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_history(terminal_state_t *term, int argc, char **argv);

/* Info commands */
term_status_t cmd_info_system(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_info_memory(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_info_disk(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_info_cpu(terminal_state_t *term, int argc, char **argv);

/* Sys commands (require sys prefix) */
term_status_t cmd_sys_status(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_sys_reboot(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_sys_shutdown(terminal_state_t *term, int argc, char **argv);

/* File operations */
term_status_t cmd_pwd(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_cd(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_list(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_show(terminal_state_t *term, int argc, char **argv);

/* Process control */
term_status_t cmd_tasks(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_uptime(terminal_state_t *term, int argc, char **argv);

/* Network */
term_status_t cmd_net_status(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_ping(terminal_state_t *term, int argc, char **argv);

/* NPA Package Manager */
term_status_t cmd_npa_help(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_npa_install(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_npa_remove(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_npa_list(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_npa_search(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_npa_update(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_npa_info(terminal_state_t *term, int argc, char **argv);
term_status_t cmd_npa_verify(terminal_state_t *term, int argc, char **argv);

#endif /* NEOLYX_TERMINAL_H */
