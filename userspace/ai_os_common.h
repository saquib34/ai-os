#ifndef AI_OS_COMMON_H
#define AI_OS_COMMON_H

#include <time.h>
#include <sys/types.h>

/* Process context structure */
typedef struct {
    char current_directory[1024];
    char username[64];
    char shell[64];
    char hostname[64];
    char git_branch[128];
    char git_status[256];
    char recent_commands[50][256];
    int command_count;
    char file_listing[1024];
    char system_info[512];
    time_t last_update;
    pid_t process_id;
    uid_t user_id;
    char env_vars[2048];
    char running_processes[4096];
    char open_ports[1024];
    char disk_usage[1024];
} ai_context_t;

/* Kernel/bridge shared structures */
typedef struct {
    int enabled;
    int debug_mode;
    int active_contexts;
    int active_requests;
    unsigned long long total_requests;
    unsigned long long successful_interpretations;
    unsigned long long failed_interpretations;
    unsigned long long blocked_commands;
} ai_os_status_t;

typedef struct {
    int enabled;
    int debug_mode;
    int safety_mode;
    int confirmation_required;
    char model_name[64];
} ai_os_config_t;

typedef struct {
    int request_id;
    pid_t pid;
    uid_t uid;
    char command[1024];
    char context[2048];
    unsigned long timestamp;
} ai_os_request_t;

typedef struct {
    int request_id;
    int result_code;
    char interpreted_command[1024];
    char error_message[256];
} ai_os_response_t;

typedef struct {
    int socket_fd;
    pid_t client_pid;
    uid_t client_uid;
    pthread_t thread_id;
    ai_context_t context;
    int active;
    time_t last_activity;
} ai_client_t;

/* Function declarations */
int ai_client_connect(void);
void ai_client_disconnect(void);
int ai_interpret_command(const char *natural_command, char *shell_command, size_t command_size);
int ai_execute_command(const char *command, char *output, size_t output_size);
int ai_get_status(char *status_info, size_t info_size);
int ai_set_model(const char *model_name);
int ai_get_context(char *context_info, size_t info_size);

int ollama_client_init(const char *model_name, const char *api_url);
int ollama_interpret_command(const char *natural_command, const char *context, 
                            char *shell_command, size_t command_size);
int ollama_check_status(void);
int ollama_list_models(char *models_list, size_t list_size);
int ollama_set_model(const char *model_name);
void ollama_client_cleanup(void);

int ai_context_create(ai_context_t *ctx, pid_t pid);
char *ai_context_to_json(const ai_context_t *ctx);
char *ai_context_to_summary(const ai_context_t *ctx);
int ai_context_update(ai_context_t *ctx);
int ai_context_add_command(ai_context_t *ctx, const char *command);
int ai_context_needs_refresh(ai_context_t *ctx);
void ai_context_free(ai_context_t *ctx);

int kernel_bridge_init(void);
int kernel_bridge_start(void);
void kernel_bridge_stop(void);
void kernel_bridge_cleanup(void);

#endif /* AI_OS_COMMON_H */ 