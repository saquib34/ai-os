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

#define MAX_PATH_SIZE 1024
#define MAX_HISTORY_ENTRIES 50

/* AI Context structure */
typedef struct {
    char current_directory[MAX_PATH_SIZE];
    char username[64];
    char shell[64];
    char hostname[64];
    char git_branch[128];
    char git_status[256];
    char recent_commands[MAX_HISTORY_ENTRIES][256];
    int command_count;
    char file_listing[1024];
    char system_info[512];
    time_t last_update;
    pid_t process_id;
    uid_t user_id;
} ai_context_t;

/* Get current working directory */
static int get_current_directory(ai_context_t *ctx) {
    if (getcwd(ctx->current_directory, sizeof(ctx->current_directory)) == NULL) {
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
