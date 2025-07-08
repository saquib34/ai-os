/*
 * Context Manager for AI-OS
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <time.h>
#include <json-c/json.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include "../ai_os_common.h"

#define MAX_PATH_SIZE 1024
#define MAX_HISTORY_ENTRIES 50
#define CONTEXT_MANAGER_LOG_FILE "/var/log/ai-os/context_manager.log"
#define CONTEXT_MANAGER_LOG_MAX_SIZE (1024 * 1024) // 1MB
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static FILE *log_file = NULL;
static void context_manager_rotate_log() {
    struct stat st;
    if (stat(CONTEXT_MANAGER_LOG_FILE, &st) == 0 && st.st_size > CONTEXT_MANAGER_LOG_MAX_SIZE) {
        char rotated[512];
        snprintf(rotated, sizeof(rotated), "%s.old", CONTEXT_MANAGER_LOG_FILE);
        rename(CONTEXT_MANAGER_LOG_FILE, rotated);
    }
}
static void context_manager_log(const char *fmt, ...) {
    pthread_mutex_lock(&log_mutex);
    context_manager_rotate_log();
    if (!log_file) {
        log_file = fopen(CONTEXT_MANAGER_LOG_FILE, "a");
        if (!log_file) log_file = stderr;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    fflush(log_file);
    va_end(args);
    pthread_mutex_unlock(&log_mutex);
}

/* Get current working directory */
static int get_current_directory(ai_context_t *ctx) {
    if (getcwd(ctx->current_directory, sizeof(ctx->current_directory)) == NULL) {
        context_manager_log("[AI-OS Context] getcwd failed: %s\n", strerror(errno));
        strcpy(ctx->current_directory, "/");
        return -1;
    }
    return 0;
}

/* Get username and user info */
static int get_user_info(ai_context_t *ctx) {
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        strncpy(ctx->username, pw->pw_name, sizeof(ctx->username) - 1);
        strncpy(ctx->shell, pw->pw_shell, sizeof(ctx->shell) - 1);
    } else {
        strcpy(ctx->username, "unknown");
        strcpy(ctx->shell, "/bin/bash");
    }
    
    ctx->user_id = getuid();
    return 0;
}

/* Get hostname */
static int get_hostname(ai_context_t *ctx) {
    if (gethostname(ctx->hostname, sizeof(ctx->hostname)) != 0) {
        strcpy(ctx->hostname, "localhost");
        return -1;
    }
    return 0;
}

/* Create comprehensive context */
int ai_context_create(ai_context_t *ctx, pid_t pid) {
    if (!ctx) return -1;
    
    memset(ctx, 0, sizeof(ai_context_t));
    ctx->process_id = pid;
    ctx->last_update = time(NULL);
    
    /* Gather all context information */
    get_current_directory(ctx);
    get_user_info(ctx);
    get_hostname(ctx);
    
    // Gather environment variables (first 2kB)
    FILE *envf = fopen("/proc/self/environ", "r");
    if (envf) {
        size_t n = fread(ctx->env_vars, 1, sizeof(ctx->env_vars)-1, envf);
        ctx->env_vars[n] = '\0';
        if (n == sizeof(ctx->env_vars)-1) {
            context_manager_log("[AI-OS Context] Warning: env_vars truncated\n");
        }
        fclose(envf);
    } else {
        context_manager_log("[AI-OS Context] Failed to open /proc/self/environ: %s\n", strerror(errno));
        strcpy(ctx->env_vars, "");
    }
    // Gather running processes (ps aux, first 4kB)
    FILE *psf = popen("ps aux --no-heading | head -n 20", "r");
    if (psf) {
        size_t n = fread(ctx->running_processes, 1, sizeof(ctx->running_processes)-1, psf);
        ctx->running_processes[n] = '\0';
        if (n == sizeof(ctx->running_processes)-1) {
            context_manager_log("[AI-OS Context] Warning: running_processes truncated\n");
        }
        pclose(psf);
    } else {
        context_manager_log("[AI-OS Context] Failed to run ps aux: %s\n", strerror(errno));
        strcpy(ctx->running_processes, "");
    }
    // Gather open network ports (ss -tuln, first 1kB)
    FILE *ssf = popen("ss -tuln | head -n 20", "r");
    if (ssf) {
        size_t n = fread(ctx->open_ports, 1, sizeof(ctx->open_ports)-1, ssf);
        ctx->open_ports[n] = '\0';
        if (n == sizeof(ctx->open_ports)-1) {
            context_manager_log("[AI-OS Context] Warning: open_ports truncated\n");
        }
        pclose(ssf);
    } else {
        context_manager_log("[AI-OS Context] Failed to run ss -tuln: %s\n", strerror(errno));
        strcpy(ctx->open_ports, "");
    }
    // Gather disk usage (df -h, first 1kB)
    FILE *dff = popen("df -h | head -n 10", "r");
    if (dff) {
        size_t n = fread(ctx->disk_usage, 1, sizeof(ctx->disk_usage)-1, dff);
        ctx->disk_usage[n] = '\0';
        if (n == sizeof(ctx->disk_usage)-1) {
            context_manager_log("[AI-OS Context] Warning: disk_usage truncated\n");
        }
        pclose(dff);
    } else {
        context_manager_log("[AI-OS Context] Failed to run df -h: %s\n", strerror(errno));
        strcpy(ctx->disk_usage, "");
    }
    
    return 0;
}

/* Convert context to summary string */
char *ai_context_to_summary(const ai_context_t *ctx) {
    static char summary[1024];
    
    if (!ctx) return NULL;
    
    snprintf(summary, sizeof(summary),
             "User: %s@%s in %s",
             ctx->username,
             ctx->hostname,
             ctx->current_directory);
    
    return summary;
}

/* Add command to history */
int ai_context_add_command(ai_context_t *ctx, const char *command) {
    if (!ctx || !command) return -1;
    
    /* Shift existing commands if needed */
    if (ctx->command_count >= MAX_HISTORY_ENTRIES) {
        for (int i = 0; i < MAX_HISTORY_ENTRIES - 1; i++) {
            strcpy(ctx->recent_commands[i], ctx->recent_commands[i + 1]);
        }
        ctx->command_count = MAX_HISTORY_ENTRIES - 1;
    }
    
    /* Add new command */
    strncpy(ctx->recent_commands[ctx->command_count], command, 
            sizeof(ctx->recent_commands[0]) - 1);
    ctx->recent_commands[ctx->command_count][sizeof(ctx->recent_commands[0]) - 1] = '\0';
    ctx->command_count++;
    
    return 0;
}

// Update all context fields (refresh)
int ai_context_update(ai_context_t *ctx) {
    if (!ctx) return -1;
    get_current_directory(ctx);
    get_user_info(ctx);
    get_hostname(ctx);
    ctx->last_update = time(NULL);
    return 0;
}

// Check if context needs refresh (older than 5 seconds)
int ai_context_needs_refresh(ai_context_t *ctx) {
    if (!ctx) return 1;
    time_t now = time(NULL);
    return (now - ctx->last_update) > 5;
}

// Convert context to JSON string (caller must free)
char *ai_context_to_json(const ai_context_t *ctx) {
    if (!ctx) return NULL;
    json_object *obj = json_object_new_object();
    json_object_object_add(obj, "current_directory", json_object_new_string(ctx->current_directory));
    json_object_object_add(obj, "username", json_object_new_string(ctx->username));
    json_object_object_add(obj, "shell", json_object_new_string(ctx->shell));
    json_object_object_add(obj, "hostname", json_object_new_string(ctx->hostname));
    json_object_object_add(obj, "git_branch", json_object_new_string(ctx->git_branch));
    json_object_object_add(obj, "git_status", json_object_new_string(ctx->git_status));
    json_object_object_add(obj, "file_listing", json_object_new_string(ctx->file_listing));
    json_object_object_add(obj, "system_info", json_object_new_string(ctx->system_info));
    json_object_object_add(obj, "process_id", json_object_new_int(ctx->process_id));
    json_object_object_add(obj, "user_id", json_object_new_int(ctx->user_id));
    json_object_object_add(obj, "last_update", json_object_new_int((int)ctx->last_update));
    // Add recent commands as array
    json_object *cmds = json_object_new_array();
    for (int i = 0; i < ctx->command_count; ++i) {
        json_object_array_add(cmds, json_object_new_string(ctx->recent_commands[i]));
    }
    json_object_object_add(obj, "recent_commands", cmds);
    json_object_object_add(obj, "env_vars", json_object_new_string(ctx->env_vars));
    json_object_object_add(obj, "running_processes", json_object_new_string(ctx->running_processes));
    json_object_object_add(obj, "open_ports", json_object_new_string(ctx->open_ports));
    json_object_object_add(obj, "disk_usage", json_object_new_string(ctx->disk_usage));
    char *json_str = strdup(json_object_to_json_string(obj));
    json_object_put(obj);
    return json_str;
}

// Free any dynamically allocated fields (none currently, stub for future)
void ai_context_free(ai_context_t *ctx) {
    (void)ctx;
    // No dynamic allocations yet
}

void context_manager_log_cleanup(void) {
    pthread_mutex_lock(&log_mutex);
    if (log_file && log_file != stderr) fclose(log_file);
    log_file = NULL;
    pthread_mutex_unlock(&log_mutex);
}
