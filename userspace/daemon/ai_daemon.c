/*
 * AI Operating System Daemon - Main Component
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <errno.h>
#include <json-c/json.h>
#include <syslog.h>
#include <stdarg.h>  // For va_start/va_end
#include <time.h>    // For time functions

#define AI_SOCKET_PATH "/var/run/ai-os.sock"
#define AI_CONFIG_FILE "/etc/ai-os/config.json"
#define AI_LOG_FILE "/var/log/ai-os.log"
#define MAX_CLIENTS 64
#define MAX_COMMAND_LEN 4096

/* External function declarations */
extern int ollama_client_init(const char *model_name, const char *api_url);
extern int ollama_interpret_command(const char *natural_command, const char *context, 
                                   char *shell_command, size_t command_size);
extern int ollama_check_status(void);
extern int ollama_list_models(char *models_list, size_t list_size);
extern int ollama_set_model(const char *model_name);
extern void ollama_client_cleanup(void);

/* Context structure */
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
} ai_context_t;

extern int ai_context_create(ai_context_t *ctx, pid_t pid);
extern char *ai_context_to_summary(const ai_context_t *ctx);
extern int ai_context_add_command(ai_context_t *ctx, const char *command);

/* Client structure */
typedef struct {
    int socket_fd;
    pid_t client_pid;
    uid_t client_uid;
    pthread_t thread_id;
    ai_context_t context;
    int active;
    time_t last_activity;
} ai_client_t;

/* Global daemon state */
typedef struct {
    int server_socket;
    ai_client_t clients[MAX_CLIENTS];
    pthread_mutex_t clients_mutex;
    int running;
    char current_model[64];
    int safety_mode;
    int confirmation_required;
    FILE *log_file;
} ai_daemon_t;

static ai_daemon_t g_daemon = {0};

/* Simple logging function */
static void ai_log(const char *level, const char *format, ...) {
    va_list args;
    time_t now;
    char timestamp[64];
    
    time(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    printf("[%s] %s: ", timestamp, level);
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

/* Simple safety check */
static int is_safe_command(const char *command) {
    if (!command || strlen(command) == 0) return 0;
    
    const char *dangerous[] = {
        "rm -rf /", "dd if=", "mkfs", "format", "shutdown", "reboot", NULL
    };
    
    for (int i = 0; dangerous[i]; i++) {
        if (strstr(command, dangerous[i])) {
            ai_log("WARN", "Blocked dangerous command: %s", dangerous[i]);
            return 0;
        }
    }
    return 1;
}

/* Simple request handler */
static int handle_client_request(ai_client_t *client, const char *request, char *response, size_t response_size) {
    json_object *req_obj = json_tokener_parse(request);
    if (!req_obj) {
        snprintf(response, response_size, "{\"error\": \"Invalid JSON request\"}");
        return -1;
    }
    
    json_object *action_obj, *command_obj;
    const char *action = "interpret";
    const char *command = "";
    
    if (json_object_object_get_ex(req_obj, "action", &action_obj)) {
        action = json_object_get_string(action_obj);
    }
    
    if (json_object_object_get_ex(req_obj, "command", &command_obj)) {
        command = json_object_get_string(command_obj);
    }
    
    json_object *response_obj = json_object_new_object();
    
    if (strcmp(action, "status") == 0) {
        json_object_object_add(response_obj, "daemon_status", json_object_new_string("running"));
        json_object_object_add(response_obj, "current_model", json_object_new_string(g_daemon.current_model));
        json_object_object_add(response_obj, "status", json_object_new_string("success"));
    } else if (strcmp(action, "interpret") == 0) {
        char shell_command[MAX_COMMAND_LEN] = {0};
        char *context_summary = ai_context_to_summary(&client->context);
        ai_log("INFO", "Interpreting: %s", command);
        ai_log("INFO", "Prompt to Ollama: command='%s', context='%s'", command, context_summary ? context_summary : "(null)");
        int result = ollama_interpret_command(command, context_summary, shell_command, sizeof(shell_command));
        ai_log("INFO", "Ollama response: '%s' (result=%d)", shell_command, result);
        // Validate the interpreted command
        if (result == 0 && shell_command[0] != '\0') {
            json_object_object_add(response_obj, "interpreted_command", json_object_new_string(shell_command));
            json_object_object_add(response_obj, "status", json_object_new_string("success"));
        } else {
            json_object_object_add(response_obj, "status", json_object_new_string("error"));
            json_object_object_add(response_obj, "message", json_object_new_string("Failed to interpret or invalid response"));
        }
    } else {
        json_object_object_add(response_obj, "status", json_object_new_string("error"));
        json_object_object_add(response_obj, "message", json_object_new_string("Unknown action"));
    }
    
    const char *response_str = json_object_to_json_string(response_obj);
    strncpy(response, response_str, response_size - 1);
    response[response_size - 1] = '\0';
    
    json_object_put(req_obj);
    json_object_put(response_obj);
    
    return 0;
}

/* Client thread */
static void *client_thread(void *arg) {
    ai_client_t *client = (ai_client_t *)arg;
    char buffer[MAX_COMMAND_LEN];
    char response[MAX_COMMAND_LEN * 2];
    ssize_t bytes_received;
    
    ai_log("INFO", "Client connected");
    ai_context_create(&client->context, client->client_pid);
    
    while (client->active && g_daemon.running) {
        bytes_received = recv(client->socket_fd, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_received <= 0) {
            break;
        }
        
        buffer[bytes_received] = '\0';
        client->last_activity = time(NULL);
        
        if (handle_client_request(client, buffer, response, sizeof(response)) == 0) {
            send(client->socket_fd, response, strlen(response), 0);
        }
    }
    
    ai_log("INFO", "Client disconnected");
    close(client->socket_fd);
    client->active = 0;
    
    return NULL;
}

/* Signal handler */
static void signal_handler(int sig) {
    ai_log("INFO", "Received signal %d, shutting down", sig);
    g_daemon.running = 0;
}

/* Initialize daemon */
static int init_daemon(void) {
    struct sockaddr_un addr;
    
    ai_log("INFO", "Starting AI-OS Daemon");
    
    strcpy(g_daemon.current_model, "phi3:mini");
    g_daemon.safety_mode = 1;
    g_daemon.confirmation_required = 1;
    
    /* Initialize Ollama client */
    if (ollama_client_init(g_daemon.current_model, NULL) != 0) {
        ai_log("WARN", "Failed to initialize Ollama client");
    }
    
    /* Create server socket */
    g_daemon.server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_daemon.server_socket < 0) {
        ai_log("ERROR", "Failed to create server socket");
        return -1;
    }
    
    unlink(AI_SOCKET_PATH);
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, AI_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (bind(g_daemon.server_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ai_log("ERROR", "Failed to bind socket");
        return -1;
    }
    
    chmod(AI_SOCKET_PATH, 0666);
    
    if (listen(g_daemon.server_socket, 10) < 0) {
        ai_log("ERROR", "Failed to listen on socket");
        return -1;
    }
    
    pthread_mutex_init(&g_daemon.clients_mutex, NULL);
    g_daemon.running = 1;
    
    ai_log("INFO", "Daemon initialized successfully");
    return 0;
}

/* Main function */
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    if (init_daemon() != 0) {
        fprintf(stderr, "Failed to initialize daemon\n");
        return 1;
    }
    
    ai_log("INFO", "Daemon ready, waiting for connections...");
    
    while (g_daemon.running) {
        struct sockaddr_un client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd = accept(g_daemon.server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            ai_log("ERROR", "Failed to accept client connection: %s", strerror(errno));
            continue;
        }

        // Find a free client slot
        pthread_mutex_lock(&g_daemon.clients_mutex);
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (!g_daemon.clients[i].active) {
                slot = i;
                break;
            }
        }
        if (slot == -1) {
            ai_log("WARN", "Max clients reached, rejecting connection");
            close(client_fd);
            pthread_mutex_unlock(&g_daemon.clients_mutex);
            continue;
        }

        ai_client_t *client = &g_daemon.clients[slot];
        memset(client, 0, sizeof(ai_client_t));
        client->socket_fd = client_fd;
        client->client_pid = getpid();
        client->client_uid = getuid();
        client->active = 1;
        client->last_activity = time(NULL);

        pthread_create(&client->thread_id, NULL, client_thread, client);
        pthread_mutex_unlock(&g_daemon.clients_mutex);
    }
    
    close(g_daemon.server_socket);
    unlink(AI_SOCKET_PATH);
    ollama_client_cleanup();
    pthread_mutex_destroy(&g_daemon.clients_mutex);
    
    ai_log("INFO", "Daemon shutdown complete");
    return 0;
}
