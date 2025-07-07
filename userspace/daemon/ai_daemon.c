/*
 * AI Operating System Daemon - Main Component
 * File: userspace/daemon/ai_daemon.c
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
 #include <stdarg.h>
 
 /* Include our custom headers */
 extern int ollama_client_init(const char *model_name, const char *api_url);
 extern int ollama_interpret_command(const char *natural_command, const char *context, 
                                    char *shell_command, size_t command_size);
 extern int ollama_check_status(void);
 extern int ollama_list_models(char *models_list, size_t list_size);
 extern int ollama_set_model(const char *model_name);
 extern void ollama_client_cleanup(void);
 
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
 extern char *ai_context_to_json(const ai_context_t *ctx);
 extern char *ai_context_to_summary(const ai_context_t *ctx);
 extern int ai_context_update(ai_context_t *ctx);
 extern int ai_context_add_command(ai_context_t *ctx, const char *command);
 extern int ai_context_needs_refresh(ai_context_t *ctx);
 extern void ai_context_free(ai_context_t *ctx);
 
 #define AI_SOCKET_PATH "/var/run/ai-os.sock"
 #define AI_CONFIG_FILE "/etc/ai-os/config.json"
 #define AI_LOG_FILE "/var/log/ai-os.log"
 #define MAX_CLIENTS 64
 #define MAX_COMMAND_LEN 4096
 
 /* Client connection structure */
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
 
 /* Logging function */
 static void ai_log(const char *level, const char *format, ...) {
     va_list args;
     time_t now;
     char timestamp[64];
     
     time(&now);
     strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
     
     if (g_daemon.log_file) {
         fprintf(g_daemon.log_file, "[%s] %s: ", timestamp, level);
         va_start(args, format);
         vfprintf(g_daemon.log_file, format, args);
         va_end(args);
         fprintf(g_daemon.log_file, "\n");
         fflush(g_daemon.log_file);
     }
     
     /* Also log to syslog */
     int priority = LOG_INFO;
     if (strcmp(level, "ERROR") == 0) priority = LOG_ERR;
     else if (strcmp(level, "WARN") == 0) priority = LOG_WARNING;
     
     va_start(args, format);
     vsyslog(priority, format, args);
     va_end(args);
 }
 
 /* Safety check for commands */
 static int is_safe_command(const char *command) {
     if (!command || strlen(command) == 0) return 0;
     
     /* List of dangerous command patterns */
     const char *dangerous_patterns[] = {
         "rm -rf /",
         "rm -rf /*",
         "dd if=",
         "mkfs",
         "format",
         "fdisk",
         "parted",
         "shutdown",
         "reboot",
         "halt",
         "poweroff",
         "kill -9 1",
         "chmod 777 /",
         "chown root:root /",
         "> /dev/sda",
         "> /dev/sdb",
         "wget http://", /* Block direct downloads without review */
         "curl http://",
         ":(){ :|:& };:", /* Fork bomb */
         NULL
     };
     
     for (int i = 0; dangerous_patterns[i]; i++) {
         if (strstr(command, dangerous_patterns[i])) {
             ai_log("WARN", "Blocked dangerous command pattern: %s", dangerous_patterns[i]);
             return 0;
         }
     }
     
     /* Additional checks for sudo commands */
     if (strstr(command, "sudo ")) {
         const char *sudo_dangerous[] = {
             "sudo rm -rf",
             "sudo dd",
             "sudo mkfs",
             "sudo fdisk",
             "sudo parted",
             NULL
         };
         
         for (int i = 0; sudo_dangerous[i]; i++) {
             if (strstr(command, sudo_dangerous[i])) {
                 ai_log("WARN", "Blocked dangerous sudo command: %s", sudo_dangerous[i]);
                 return 0;
             }
         }
     }
     
     return 1;
 }
 
 /* Execute command with safety checks */
 static int execute_command_safely(ai_client_t *client, const char *command, char *output, size_t output_size) {
     if (!is_safe_command(command)) {
         snprintf(output, output_size, "ERROR: Command blocked by safety filter");
         return -1;
     }
     
     ai_log("INFO", "Executing command for PID %d: %s", client->client_pid, command);
     
     /* Add command to client's history */
     ai_context_add_command(&client->context, command);
     
     /* If in confirmation mode, don't execute automatically */
     if (g_daemon.confirmation_required) {
         snprintf(output, output_size, "CONFIRM_REQUIRED: %s", command);
         return 1; /* Needs confirmation */
     }
     
     /* Execute the command */
     FILE *fp = popen(command, "r");
     if (!fp) {
         snprintf(output, output_size, "ERROR: Failed to execute command");
         return -1;
     }
     
     size_t total_read = 0;
     char buffer[1024];
     output[0] = '\0';
     
     while (fgets(buffer, sizeof(buffer), fp) && total_read < output_size - 1) {
         size_t len = strlen(buffer);
         if (total_read + len < output_size - 1) {
             strcat(output, buffer);
             total_read += len;
         } else {
             break;
         }
     }
     
     int exit_code = pclose(fp);
     
     if (total_read == 0) {
         snprintf(output, output_size, "Command executed successfully (exit code: %d)", 
                  WEXITSTATUS(exit_code));
     }
     
     return WEXITSTATUS(exit_code);
 }
 
 /* Handle client request */
 static int handle_client_request(ai_client_t *client, const char *request, char *response, size_t response_size) {
     json_object *req_obj = json_tokener_parse(request);
     if (!req_obj) {
         snprintf(response, response_size, "{\"error\": \"Invalid JSON request\"}");
         return -1;
     }
     
     json_object *action_obj, *command_obj, *model_obj;
     const char *action = "interpret";
     const char *command = "";
     const char *model = NULL;
     
     if (json_object_object_get_ex(req_obj, "action", &action_obj)) {
         action = json_object_get_string(action_obj);
     }
     
     if (json_object_object_get_ex(req_obj, "command", &command_obj)) {
         command = json_object_get_string(command_obj);
     }
     
     if (json_object_object_get_ex(req_obj, "model", &model_obj)) {
         model = json_object_get_string(model_obj);
     }
     
     /* Update client context */
     if (ai_context_needs_refresh(&client->context)) {
         ai_context_update(&client->context);
     }
     
     json_object *response_obj = json_object_new_object();
     
     if (strcmp(action, "interpret") == 0) {
         char shell_command[MAX_COMMAND_LEN];
         char *context_summary = ai_context_to_summary(&client->context);
         
         ai_log("INFO", "Interpreting command from PID %d: %s", client->client_pid, command);
         
         int result = ollama_interpret_command(command, context_summary, shell_command, sizeof(shell_command));
         
         if (result == 0) {
             json_object_object_add(response_obj, "interpreted_command", json_object_new_string(shell_command));
             json_object_object_add(response_obj, "status", json_object_new_string("success"));
             
             /* If auto-execute is enabled and command is safe */
             if (!g_daemon.confirmation_required && is_safe_command(shell_command)) {
                 char exec_output[4096];
                 int exec_result = execute_command_safely(client, shell_command, exec_output, sizeof(exec_output));
                 
                 json_object_object_add(response_obj, "execution_result", json_object_new_string(exec_output));
                 json_object_object_add(response_obj, "exit_code", json_object_new_int(exec_result));
             }
         } else if (result == -2) {
             json_object_object_add(response_obj, "status", json_object_new_string("unsafe"));
             json_object_object_add(response_obj, "message", json_object_new_string("Command marked as unsafe by AI"));
         } else if (result == -3) {
             json_object_object_add(response_obj, "status", json_object_new_string("unclear"));
             json_object_object_add(response_obj, "message", json_object_new_string("Command unclear, please rephrase"));
         } else {
             json_object_object_add(response_obj, "status", json_object_new_string("error"));
             json_object_object_add(response_obj, "message", json_object_new_string("Failed to interpret command"));
         }
         
         free(context_summary);
         
     } else if (strcmp(action, "execute") == 0) {
         /* Direct execution request */
         char exec_output[4096];
         int exec_result = execute_command_safely(client, command, exec_output, sizeof(exec_output));
         
         json_object_object_add(response_obj, "execution_result", json_object_new_string(exec_output));
         json_object_object_add(response_obj, "exit_code", json_object_new_int(exec_result));
         json_object_object_add(response_obj, "status", json_object_new_string(exec_result == 0 ? "success" : "error"));
         
     } else if (strcmp(action, "status") == 0) {
         /* Return daemon and Ollama status */
         char models_list[1024];
         int ollama_status = ollama_check_status();
         ollama_list_models(models_list, sizeof(models_list));
         
         json_object_object_add(response_obj, "daemon_status", json_object_new_string("running"));
         json_object_object_add(response_obj, "ollama_status", json_object_new_string(ollama_status == 0 ? "running" : "not available"));
         json_object_object_add(response_obj, "current_model", json_object_new_string(g_daemon.current_model));
         json_object_object_add(response_obj, "available_models", json_object_new_string(models_list));
         json_object_object_add(response_obj, "safety_mode", json_object_new_boolean(g_daemon.safety_mode));
         json_object_object_add(response_obj, "confirmation_required", json_object_new_boolean(g_daemon.confirmation_required));
         
     } else if (strcmp(action, "set_model") == 0 && model) {
         /* Change AI model */
         if (ollama_set_model(model) == 0) {
             strncpy(g_daemon.current_model, model, sizeof(g_daemon.current_model) - 1);
             json_object_object_add(response_obj, "status", json_object_new_string("success"));
             json_object_object_add(response_obj, "message", json_object_new_string("Model changed successfully"));
             ai_log("INFO", "Model changed to: %s", model);
         } else {
             json_object_object_add(response_obj, "status", json_object_new_string("error"));
             json_object_object_add(response_obj, "message", json_object_new_string("Failed to change model"));
         }
         
     } else if (strcmp(action, "get_context") == 0) {
         /* Return current context */
         char *context_json = ai_context_to_json(&client->context);
         if (context_json) {
             json_object *context_obj = json_tokener_parse(context_json);
             json_object_object_add(response_obj, "context", context_obj);
             free(context_json);
         }
         json_object_object_add(response_obj, "status", json_object_new_string("success"));
         
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
 
 /* Client thread function */
 static void *client_thread(void *arg) {
     ai_client_t *client = (ai_client_t *)arg;
     char buffer[MAX_COMMAND_LEN];
     char response[MAX_COMMAND_LEN * 2];
     ssize_t bytes_received;
     
     ai_log("INFO", "Client connected: PID %d, UID %d", client->client_pid, client->client_uid);
     
     /* Initialize client context */
     ai_context_create(&client->context, client->client_pid);
     
     while (client->active && g_daemon.running) {
         bytes_received = recv(client->socket_fd, buffer, sizeof(buffer) - 1, 0);
         
         if (bytes_received <= 0) {
             break; /* Client disconnected */
         }
         
         buffer[bytes_received] = '\0';
         client->last_activity = time(NULL);
         
         /* Handle the request */
         if (handle_client_request(client, buffer, response, sizeof(response)) == 0) {
             send(client->socket_fd, response, strlen(response), 0);
         } else {
             const char *error_response = "{\"error\": \"Failed to process request\"}";
             send(client->socket_fd, error_response, strlen(error_response), 0);
         }
     }
     
     ai_log("INFO", "Client disconnected: PID %d", client->client_pid);
     close(client->socket_fd);
     ai_context_free(&client->context);
     client->active = 0;
     
     return NULL;
 }
 
 /* Accept new client connections */
 static int accept_client_connection(int server_socket) {
     struct sockaddr_un client_addr;
     socklen_t addr_len = sizeof(client_addr);
     int client_socket;
     
     client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);
     if (client_socket < 0) {
         if (errno != EINTR) {
             ai_log("ERROR", "Failed to accept client connection: %s", strerror(errno));
         }
         return -1;
     }
     
     /* Find available client slot */
     pthread_mutex_lock(&g_daemon.clients_mutex);
     
     for (int i = 0; i < MAX_CLIENTS; i++) {
         if (!g_daemon.clients[i].active) {
             g_daemon.clients[i].socket_fd = client_socket;
             g_daemon.clients[i].client_pid = 0; /* Will be set by client */
             g_daemon.clients[i].client_uid = getuid(); /* Default to current user */
             g_daemon.clients[i].active = 1;
             g_daemon.clients[i].last_activity = time(NULL);
             
             if (pthread_create(&g_daemon.clients[i].thread_id, NULL, client_thread, &g_daemon.clients[i]) != 0) {
                 ai_log("ERROR", "Failed to create client thread: %s", strerror(errno));
                 close(client_socket);
                 g_daemon.clients[i].active = 0;
                 pthread_mutex_unlock(&g_daemon.clients_mutex);
                 return -1;
             }
             
             pthread_mutex_unlock(&g_daemon.clients_mutex);
             return 0;
         }
     }
     
     pthread_mutex_unlock(&g_daemon.clients_mutex);
     
     ai_log("WARN", "Too many clients, rejecting connection");
     close(client_socket);
     return -1;
 }
 
 /* Load configuration */
 static int load_config(void) {
     FILE *fp = fopen(AI_CONFIG_FILE, "r");
     if (!fp) {
         ai_log("WARN", "No config file found, using defaults");
         strcpy(g_daemon.current_model, "codellama:7b-instruct");
         g_daemon.safety_mode = 1;
         g_daemon.confirmation_required = 1;
         return 0;
     }
     
     char buffer[4096];
     size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, fp);
     fclose(fp);
     
     if (bytes_read == 0) {
         ai_log("ERROR", "Failed to read config file");
         return -1;
     }
     
     buffer[bytes_read] = '\0';
     
     json_object *config = json_tokener_parse(buffer);
     if (!config) {
         ai_log("ERROR", "Invalid JSON in config file");
         return -1;
     }
     
     json_object *model_obj, *safety_obj, *confirm_obj;
     
     if (json_object_object_get_ex(config, "model", &model_obj)) {
         strncpy(g_daemon.current_model, json_object_get_string(model_obj), sizeof(g_daemon.current_model) - 1);
     }
     
     if (json_object_object_get_ex(config, "safety_mode", &safety_obj)) {
         g_daemon.safety_mode = json_object_get_boolean(safety_obj);
     }
     
     if (json_object_object_get_ex(config, "confirmation_required", &confirm_obj)) {
         g_daemon.confirmation_required = json_object_get_boolean(confirm_obj);
     }
     
     json_object_put(config);
     
     ai_log("INFO", "Configuration loaded: model=%s, safety=%d, confirm=%d", 
            g_daemon.current_model, g_daemon.safety_mode, g_daemon.confirmation_required);
     
     return 0;
 }
 
 /* Signal handler */
 static void signal_handler(int sig) {
     ai_log("INFO", "Received signal %d, shutting down daemon", sig);
     g_daemon.running = 0;
 }
 
 /* Initialize daemon */
 static int init_daemon(void) {
     struct sockaddr_un addr;
     
     /* Open log file */
     g_daemon.log_file = fopen(AI_LOG_FILE, "a");
     if (!g_daemon.log_file) {
         fprintf(stderr, "Warning: Could not open log file %s\n", AI_LOG_FILE);
     }
     
     /* Initialize syslog */
     openlog("ai-os-daemon", LOG_PID, LOG_DAEMON);
     
     ai_log("INFO", "Starting AI-OS Daemon");
     
     /* Load configuration */
     load_config();
     
     /* Initialize Ollama client */
     if (ollama_client_init(g_daemon.current_model, NULL) != 0) {
         ai_log("ERROR", "Failed to initialize Ollama client");
         return -1;
     }
     
     /* Check if Ollama is running */
     if (ollama_check_status() != 0) {
         ai_log("WARN", "Ollama is not running, some features may not work");
     }
     
     /* Create server socket */
     g_daemon.server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
     if (g_daemon.server_socket < 0) {
         ai_log("ERROR", "Failed to create server socket: %s", strerror(errno));
         return -1;
     }
     
     /* Remove existing socket file */
     unlink(AI_SOCKET_PATH);
     
     /* Bind socket */
     memset(&addr, 0, sizeof(addr));
     addr.sun_family = AF_UNIX;
     strncpy(addr.sun_path, AI_SOCKET_PATH, sizeof(addr.sun_path) - 1);
     
     if (bind(g_daemon.server_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
         ai_log("ERROR", "Failed to bind socket: %s", strerror(errno));
         return -1;
     }
     
     /* Set socket permissions */
     chmod(AI_SOCKET_PATH, 0666);
     
     /* Listen for connections */
     if (listen(g_daemon.server_socket, 10) < 0) {
         ai_log("ERROR", "Failed to listen on socket: %s", strerror(errno));
         return -1;
     }
     
     /* Initialize mutex */
     pthread_mutex_init(&g_daemon.clients_mutex, NULL);
     
     g_daemon.running = 1;
     
     ai_log("INFO", "AI-OS Daemon initialized successfully");
     return 0;
 }
 
 /* Cleanup daemon */
 static void cleanup_daemon(void) {
     ai_log("INFO", "Cleaning up AI-OS Daemon");
     
     /* Stop accepting new connections */
     g_daemon.running = 0;
     
     /* Close client connections */
     pthread_mutex_lock(&g_daemon.clients_mutex);
     for (int i = 0; i < MAX_CLIENTS; i++) {
         if (g_daemon.clients[i].active) {
             g_daemon.clients[i].active = 0;
             close(g_daemon.clients[i].socket_fd);
             pthread_join(g_daemon.clients[i].thread_id, NULL);
         }
     }
     pthread_mutex_unlock(&g_daemon.clients_mutex);
     
     /* Close server socket */
     close(g_daemon.server_socket);
     unlink(AI_SOCKET_PATH);
     
     /* Cleanup Ollama client */
     ollama_client_cleanup();
     
     /* Cleanup mutex */
     pthread_mutex_destroy(&g_daemon.clients_mutex);
     
     /* Close log file */
     if (g_daemon.log_file) {
         fclose(g_daemon.log_file);
     }
     
     closelog();
     
     ai_log("INFO", "AI-OS Daemon cleanup complete");
 }
 
 /* Main daemon loop */
 static void daemon_main_loop(void) {
     ai_log("INFO", "Starting main daemon loop");
     
     while (g_daemon.running) {
         if (accept_client_connection(g_daemon.server_socket) < 0) {
             if (g_daemon.running && errno != EINTR) {
                 usleep(100000); /* Sleep 100ms on error */
             }
         }
     }
 }
 
 /* Main function */
 int main(int argc, char *argv[]) {
     /* Check if running as root */
     if (getuid() == 0) {
         fprintf(stderr, "Warning: Running as root is not recommended\n");
     }
     
     /* Setup signal handlers */
     signal(SIGINT, signal_handler);
     signal(SIGTERM, signal_handler);
     signal(SIGPIPE, SIG_IGN); /* Ignore broken pipe */
     
     /* Initialize daemon */
     if (init_daemon() != 0) {
         fprintf(stderr, "Failed to initialize daemon\n");
         return 1;
     }
     
     /* Run main loop */
     daemon_main_loop();
     
     /* Cleanup */
     cleanup_daemon();
     
     return 0;
 }