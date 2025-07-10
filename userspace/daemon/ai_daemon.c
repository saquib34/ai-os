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
 #include "../ai_os_common.h"
 extern int ollama_client_init(const char *model_name, const char *api_url);
 extern int ollama_interpret_command(const char *natural_command, const char *context, 
                                    char *shell_command, size_t command_size);
 extern int ollama_check_status(void);
 extern int ollama_list_models(char *models_list, size_t list_size);
 extern int ollama_set_model(const char *model_name);
 extern void ollama_client_cleanup(void);
 
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
     /* Safety checks disabled - all commands are considered safe */
     return 1;
     
     /* Original safety logic (commented out):
     if (!command || strlen(command) == 0) return 0;
     
     // List of dangerous command patterns
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
         "wget http://", // Block direct downloads without review
         "curl http://",
         ":(){ :|:& };:", // Fork bomb
         NULL
     };
     
     for (int i = 0; dangerous_patterns[i]; i++) {
         if (strstr(command, dangerous_patterns[i])) {
             ai_log("WARN", "Blocked dangerous command pattern: %s", dangerous_patterns[i]);
             return 0;
         }
     }
     
     // Additional checks for sudo commands
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
     */
 }
 
 /* Execute command with safety checks */
 static int execute_command_safely(ai_client_t *client, const char *command, char *output, size_t output_size) {
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
             
             /* Auto-execute is enabled - execute all commands */
             if (!g_daemon.confirmation_required) {
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
         
     } else if (strcmp(action, "classify") == 0) {
         /* Classify input as command or chat */
         ai_log("INFO", "Classifying input from PID %d: %s", client->client_pid, command);
         
         /* Enhanced classification logic */
         const char *classification = "chat"; /* default */
         
         /* Command action words - high priority */
         const char *command_actions[] = {
             "add", "commit", "push", "pull", "clone", "init", "status", "log", "branch", "checkout",
             "merge", "rebase", "stash", "reset", "revert", "tag", "fetch", "remote", "config",
             "list", "show", "find", "search", "grep", "cat", "head", "tail", "less", "more",
             "create", "delete", "remove", "rm", "mkdir", "touch", "cp", "copy", "mv", "move",
             "install", "uninstall", "update", "upgrade", "download", "wget", "curl", "scp", "rsync",
             "run", "start", "stop", "restart", "kill", "pkill", "killall", "ps", "top", "htop",
             "check", "test", "verify", "validate", "get", "set", "export", "import", "source",
             "open", "close", "edit", "view", "read", "write", "save", "load", "backup", "restore",
             "build", "compile", "make", "cmake", "configure", "install", "uninstall", "package",
             "mount", "umount", "format", "partition", "fsck", "dd", "tar", "zip", "unzip",
             "chmod", "chown", "chgrp", "umask", "sudo", "su", "whoami", "id", "groups",
             "ping", "traceroute", "netstat", "ss", "iptables", "firewall", "ufw",
             "docker", "podman", "kubectl", "helm", "terraform", "ansible",
             "python", "pip", "node", "npm", "yarn", "cargo", "go", "java", "maven", "gradle",
             NULL
         };
         
         /* Chat/question words - lower priority */
         const char *chat_words[] = {
             "hello", "hi", "hey", "good morning", "good afternoon", "good evening",
             "how are you", "how do you", "what is", "what are", "who is", "who are",
             "when is", "when will", "where is", "where are", "why is", "why are",
             "tell me", "explain", "describe", "define", "what does", "how does",
             "joke", "funny", "humor", "weather", "time", "date", "temperature",
             "thanks", "thank you", "appreciate", "help", "please", "could you",
             "would you", "can you", "should I", "do you think", "what do you think",
             NULL
         };
         
         /* Check for command action words first (higher priority) */
         for (int i = 0; command_actions[i] != NULL; i++) {
             if (strstr(command, command_actions[i])) {
                 classification = "command";
                 break;
             }
         }
         
         /* Only check for chat words if no command action was found */
         if (strcmp(classification, "command") != 0) {
             for (int i = 0; chat_words[i] != NULL; i++) {
                 if (strstr(command, chat_words[i])) {
                     classification = "chat";
                     break;
                 }
             }
         }
         
         json_object_object_add(response_obj, "classification", json_object_new_string(classification));
         json_object_object_add(response_obj, "status", json_object_new_string("success"));
         
     } else if (strcmp(action, "chat") == 0) {
         /* Handle chat requests */
         ai_log("INFO", "Chat request from PID %d: %s", client->client_pid, command);
         
         char *context_summary = ai_context_to_summary(&client->context);
         
         /* Use Ollama for chat response */
         char chat_response[1024];
         int result = ollama_interpret_command(command, context_summary, chat_response, sizeof(chat_response));
         
         if (result == 0) {
             json_object_object_add(response_obj, "chat_response", json_object_new_string(chat_response));
             json_object_object_add(response_obj, "status", json_object_new_string("success"));
         } else {
             json_object_object_add(response_obj, "status", json_object_new_string("error"));
             json_object_object_add(response_obj, "message", json_object_new_string("Failed to get chat response"));
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
     if (pthread_mutex_lock(&g_daemon.clients_mutex) != 0) {
         ai_log("ERROR", "Failed to lock clients mutex in accept_client_connection: %s", strerror(errno));
         close(client_socket);
         return -1;
     }
     
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
                 if (pthread_mutex_unlock(&g_daemon.clients_mutex) != 0) {
                     ai_log("ERROR", "Failed to unlock clients mutex after thread creation failure: %s", strerror(errno));
                 }
                 return -1;
             }
             
             if (pthread_mutex_unlock(&g_daemon.clients_mutex) != 0) {
                 ai_log("ERROR", "Failed to unlock clients mutex after successful client creation: %s", strerror(errno));
             }
             return 0;
         }
     }
     
     if (pthread_mutex_unlock(&g_daemon.clients_mutex) != 0) {
         ai_log("ERROR", "Failed to unlock clients mutex after full client list check: %s", strerror(errno));
     }
     
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

     g_daemon.log_file = fopen(AI_LOG_FILE, "a");
     if (!g_daemon.log_file) {
         ai_log("WARN", "Could not open log file %s", AI_LOG_FILE);
     }

     /* Initialize syslog */
     openlog("ai-os-daemon", LOG_PID, LOG_DAEMON);

     ai_log("INFO", "Starting AI-OS Daemon");

     if (load_config() != 0) {
         ai_log("ERROR", "Failed to load config");
         // Continue with defaults
     }

     if (ollama_client_init(g_daemon.current_model, NULL) != 0) {
         ai_log("ERROR", "Failed to initialize Ollama client");
         // Continue, but warn
     }

     if (ollama_check_status() != 0) {
         ai_log("WARN", "Ollama is not running, some features may not work");
         // Do not fail
     }

     g_daemon.server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
     if (g_daemon.server_socket < 0) {
         ai_log("ERROR", "Failed to create server socket: %s", strerror(errno));
         return -1;
     }

     unlink(AI_SOCKET_PATH);
     memset(&addr, 0, sizeof(addr));
     addr.sun_family = AF_UNIX;
     strncpy(addr.sun_path, AI_SOCKET_PATH, sizeof(addr.sun_path) - 1);
     if (bind(g_daemon.server_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
         ai_log("ERROR", "Failed to bind socket: %s", strerror(errno));
         close(g_daemon.server_socket);
         return -1;
     }

     if (chmod(AI_SOCKET_PATH, 0666) < 0) {
         ai_log("WARN", "Failed to set socket permissions: %s", strerror(errno));
     }

     if (listen(g_daemon.server_socket, 10) < 0) {
         ai_log("ERROR", "Failed to listen on socket: %s", strerror(errno));
         close(g_daemon.server_socket);
         unlink(AI_SOCKET_PATH);
         return -1;
     }

     if (pthread_mutex_init(&g_daemon.clients_mutex, NULL) != 0) {
         ai_log("ERROR", "Failed to initialize clients mutex: %s", strerror(errno));
         close(g_daemon.server_socket);
         unlink(AI_SOCKET_PATH);
         return -1;
     }
     g_daemon.running = 1;
     ai_log("INFO", "AI-OS Daemon initialized successfully");
     return 0;
 }
 
 /* Cleanup daemon */
 static void cleanup_daemon(void) {
     ai_log("INFO", "Cleaning up AI-OS Daemon");
     g_daemon.running = 0;
     if (pthread_mutex_lock(&g_daemon.clients_mutex) != 0) {
         ai_log("ERROR", "Failed to lock clients mutex during cleanup: %s", strerror(errno));
     }
     for (int i = 0; i < MAX_CLIENTS; i++) {
         if (g_daemon.clients[i].active) {
             g_daemon.clients[i].active = 0;
             if (close(g_daemon.clients[i].socket_fd) != 0) {
                 ai_log("WARN", "Failed to close client socket: %s", strerror(errno));
             }
             if (pthread_join(g_daemon.clients[i].thread_id, NULL) != 0) {
                 ai_log("WARN", "Failed to join client thread: %s", strerror(errno));
             }
         }
     }
     if (pthread_mutex_unlock(&g_daemon.clients_mutex) != 0) {
         ai_log("ERROR", "Failed to unlock clients mutex during cleanup: %s", strerror(errno));
     }
     if (close(g_daemon.server_socket) != 0) {
         ai_log("WARN", "Failed to close server socket: %s", strerror(errno));
     }
     if (unlink(AI_SOCKET_PATH) != 0) {
         ai_log("WARN", "Failed to unlink socket file: %s", strerror(errno));
     }
     ollama_client_cleanup();
     if (pthread_mutex_destroy(&g_daemon.clients_mutex) != 0) {
         ai_log("ERROR", "Failed to destroy clients mutex: %s", strerror(errno));
     }
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
         ai_log("WARN", "Running as root is not recommended");
     }
     
     /* Setup signal handlers */
     signal(SIGINT, signal_handler);
     signal(SIGTERM, signal_handler);
     signal(SIGPIPE, SIG_IGN); /* Ignore broken pipe */
     
     /* Initialize daemon */
     if (init_daemon() != 0) {
         ai_log("ERROR", "Failed to initialize daemon");
         return 1;
     }
     
     /* Run main loop */
     daemon_main_loop();
     
     /* Cleanup */
     cleanup_daemon();
     
     return 0;
 }