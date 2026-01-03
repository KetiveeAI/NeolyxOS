/*
 * NXAudio Daemon (nxaudiod)
 * 
 * System audio daemon for NeolyxOS:
 * - Device enumeration and management
 * - Audio routing and mixing
 * - Session management
 * - Policy enforcement
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

#include "nxaudio/nxaudio.h"

/* ============ Configuration ============ */
#define NXAUDIOD_VERSION        "1.0.0"
#define CONFIG_PATH             "/etc/nxaudio/config.yaml"
#define PID_FILE                "/run/nxaudiod.pid"
#define LOG_FILE                "/var/log/nxaudiod.log"
#define SOCKET_PATH             "/run/nxaudio.sock"

#define MAX_CLIENTS             64
#define BUFFER_SIZE             4096

/* ============ Daemon State ============ */
typedef struct {
    int                 running;
    int                 daemonize;
    FILE               *log_file;
    nxaudio_context_t   main_context;
    
    /* Audio config */
    uint32_t            sample_rate;
    uint32_t            buffer_size;
    uint32_t            channels;
    
    /* Device */
    nxaudio_device_t    output_device;
    nxaudio_device_t    input_device;
    
} daemon_state_t;

static daemon_state_t g_daemon = {0};
static volatile int g_shutdown = 0;

/* ============ Logging ============ */

static void daemon_log(const char *level, const char *msg) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    if (g_daemon.log_file) {
        fprintf(g_daemon.log_file, "[%s] [%s] %s\n", timestamp, level, msg);
        fflush(g_daemon.log_file);
    }
    
    if (!g_daemon.daemonize) {
        fprintf(stderr, "[%s] [%s] %s\n", timestamp, level, msg);
    }
}

#define LOG_INFO(msg)  daemon_log("INFO", msg)
#define LOG_WARN(msg)  daemon_log("WARN", msg)
#define LOG_ERROR(msg) daemon_log("ERROR", msg)

/* ============ Signal Handlers ============ */

static void signal_handler(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            LOG_INFO("Received shutdown signal");
            g_shutdown = 1;
            break;
        case SIGHUP:
            LOG_INFO("Received SIGHUP, reloading config");
            /* TODO: Reload config */
            break;
    }
}

static void setup_signals(void) {
    struct sigaction sa;
    
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    
    signal(SIGPIPE, SIG_IGN);
}

/* ============ PID File ============ */

static int write_pid_file(void) {
    FILE *f = fopen(PID_FILE, "w");
    if (!f) {
        LOG_ERROR("Failed to create PID file");
        return -1;
    }
    
    fprintf(f, "%d\n", getpid());
    fclose(f);
    
    return 0;
}

static void remove_pid_file(void) {
    unlink(PID_FILE);
}

/* ============ Daemonize ============ */

static int daemonize_process(void) {
    pid_t pid;
    
    /* Fork */
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid > 0) {
        /* Parent exits */
        exit(0);
    }
    
    /* Child becomes session leader */
    if (setsid() < 0) {
        return -1;
    }
    
    /* Fork again */
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid > 0) {
        exit(0);
    }
    
    /* Set working directory */
    if (chdir("/") < 0) {
        return -1;
    }
    
    /* Set file permissions */
    umask(027);
    
    /* Close standard file descriptors */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    /* Redirect to /dev/null */
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);
    
    return 0;
}

/* ============ Audio Processing ============ */

static int init_audio_system(void) {
    LOG_INFO("Initializing NXAudio system...");
    
    if (nxaudio_init() != NXAUDIO_SUCCESS) {
        LOG_ERROR("Failed to initialize NXAudio");
        return -1;
    }
    
    /* Create main context */
    nxaudio_context_config_t config = {
        .sample_rate = g_daemon.sample_rate,
        .buffer_size = g_daemon.buffer_size,
        .max_objects = 128,
        .distance_model = NXAUDIO_DISTANCE_INVERSE,
        .doppler_factor = 1.0f,
        .speed_of_sound = 343.0f,
    };
    
    g_daemon.main_context = nxaudio_context_create_ex(&config);
    if (g_daemon.main_context == NXAUDIO_INVALID_HANDLE) {
        LOG_ERROR("Failed to create audio context");
        return -1;
    }
    
    /* Load default HRTF */
    if (nxaudio_hrtf_load_default(g_daemon.main_context) != NXAUDIO_SUCCESS) {
        LOG_WARN("Failed to load default HRTF");
    }
    
    LOG_INFO("Audio system initialized");
    return 0;
}

static void cleanup_audio_system(void) {
    LOG_INFO("Cleaning up audio system...");
    
    if (g_daemon.main_context != NXAUDIO_INVALID_HANDLE) {
        nxaudio_context_destroy(g_daemon.main_context);
    }
    
    nxaudio_shutdown();
    
    LOG_INFO("Audio system cleanup complete");
}

/* ============ Main Loop ============ */

static void audio_processing_loop(void) {
    LOG_INFO("Entering audio processing loop");
    
    while (!g_shutdown) {
        /* TODO: Process audio clients */
        /* TODO: Mix and output audio */
        
        usleep(10000); /* 10ms */
    }
    
    LOG_INFO("Exiting audio processing loop");
}

/* ============ Main ============ */

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -d, --daemon      Run as daemon\n");
    printf("  -c, --config PATH Config file path\n");
    printf("  -h, --help        Show this help\n");
    printf("  -v, --version     Show version\n");
    printf("\n");
    printf("NXAudio Daemon v%s\n", NXAUDIOD_VERSION);
    printf("Copyright (c) 2025 KetiveeAI\n");
}

static void print_version(void) {
    printf("nxaudiod %s\n", NXAUDIOD_VERSION);
    printf("NXAudio library %s\n", nxaudio_version());
}

int main(int argc, char *argv[]) {
    const char *config_path = CONFIG_PATH;
    int run_daemon = 0;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--daemon") == 0) {
            run_daemon = 1;
        }
        else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                config_path = argv[++i];
            }
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
    }
    
    /* Default config */
    g_daemon.sample_rate = 48000;
    g_daemon.buffer_size = 1024;
    g_daemon.channels = 2;
    g_daemon.daemonize = run_daemon;
    
    /* Daemonize if requested */
    if (run_daemon) {
        if (daemonize_process() < 0) {
            fprintf(stderr, "Failed to daemonize\n");
            return 1;
        }
    }
    
    /* Setup logging */
    mkdir("/var/log", 0755);
    g_daemon.log_file = fopen(LOG_FILE, "a");
    
    /* Banner */
    LOG_INFO("========================================");
    LOG_INFO("  NXAudio Daemon v" NXAUDIOD_VERSION);
    LOG_INFO("  Copyright (c) 2025 KetiveeAI");
    LOG_INFO("========================================");
    
    /* Write PID file */
    if (write_pid_file() < 0) {
        return 1;
    }
    
    /* Setup signals */
    setup_signals();
    
    /* Load config */
    LOG_INFO("Loading config...");
    (void)config_path; /* TODO: Parse config */
    
    /* Initialize audio */
    if (init_audio_system() < 0) {
        remove_pid_file();
        return 1;
    }
    
    g_daemon.running = 1;
    
    /* Main loop */
    audio_processing_loop();
    
    /* Cleanup */
    cleanup_audio_system();
    remove_pid_file();
    
    if (g_daemon.log_file) {
        fclose(g_daemon.log_file);
    }
    
    LOG_INFO("Daemon shutdown complete");
    
    return 0;
}
