/*
 * AI-OS Client Library
 * File: userspace/client/ai_client.c
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <sys/socket.h>
 #include <sys/un.h>
 #include <json-c/json.h>
 #include <errno.h>
 
 #define AI_SOCKET_PATH "/var/run/ai-os.sock"
 #define MAX_RESPONSE_SIZE 8192
 
 /* Client connection structure */
 typedef struct {
     int socket_fd;
     int connected;
 } ai_client_t;
 
 static ai_client_t g_client = {-1, 0};
 
 /* Connect to AI daemon */
 int ai_client_connect(void) {
     struct sockaddr_un addr;
     
     if (g_client.connected) {
         return 0; /* Already connected */
     }
     
     /* Create socket */
     g_client.socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
     if (g_client.socket_fd < 0) {
         fprintf(stderr, "AI-Client: Failed to create socket: %s\n", strerror(errno));
         return -1;
     }
     
     /* Connect to daemon */
     memset(&addr, 0, sizeof(addr));
     addr.sun_family = AF_UNIX;
     strncpy(addr.sun_path, AI_SOCKET_PATH, sizeof(addr.sun_path) - 1);
     
     if (connect(g_client.socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
         fprintf(stderr, "AI-Client: Failed to connect to daemon: %s\n", strerror(errno));
         close(g_client.socket_fd);
         g_client.socket_fd = -1;
         return -1;
     }
     
     g_client.connected = 1;
     return 0;
 }
 
 /* Disconnect from AI daemon */
 void ai_client_disconnect(void) {
     if (g_client.connected && g_client.socket_fd >= 0) {
         close(g_client.socket_fd);
         g_client.socket_fd = -1;
         g_client.connected = 0;
     }
 }
 
 /* Send request and receive response */
 static int send_request(const char *request, char *response, size_t response_size) {
     if (!g_client.connected) {
         if (ai_client_connect() != 0) {
             return -1;
         }
     }
     
     /* Send request */
     ssize_t bytes_sent = send(g_client.socket_fd, request, strlen(request), 0);
     if (bytes_sent < 0) {
         fprintf(stderr, "AI-Client: Failed to send request: %s\n", strerror(errno));
         ai_client_disconnect();
         return -1;
     }
     
     /* Receive response */
     ssize_t bytes_received = recv(g_client.socket_fd, response, response_size - 1, 0);
     if (bytes_received < 0) {
         fprintf(stderr, "AI-Client: Failed to receive response: %s\n", strerror(errno));
         ai_client_disconnect();
         return -1;
     }
     
     response[bytes_received] = '\0';
     return 0;
 }
 
 /* Interpret natural language command */
 int ai_interpret_command(const char *natural_command, char *shell_command, size_t command_size) {
     if (!natural_command || !shell_command || command_size == 0) {
         return -1;
     }
     
     /* Create JSON request */
     json_object *request = json_object_new_object();
     json_object *action = json_object_new_string("interpret");
     json_object *command = json_object_new_string(natural_command);
     
     json_object_object_add(request, "action", action);
     json_object_object_add(request, "command", command);
     
     const char *request_str = json_object_to_json_string(request);
     
     /* Send request */
     char response[MAX_RESPONSE_SIZE];
     int result = send_request(request_str, response, sizeof(response));
     
     json_object_put(request);
     
     if (result != 0) {
         return -1;
     }
     
     /* Parse response */
     json_object *response_obj = json_tokener_parse(response);
     if (!response_obj) {
         fprintf(stderr, "AI-Client: Invalid JSON response\n");
         return -1;
     }
     
     json_object *status_obj, *interpreted_obj;
     const char *status = "error";
     
     if (json_object_object_get_ex(response_obj, "status", &status_obj)) {
         status = json_object_get_string(status_obj);
     }
     
     if (strcmp(status, "success") == 0) {
         if (json_object_object_get_ex(response_obj, "interpreted_command", &interpreted_obj)) {
             const char *interpreted = json_object_get_string(interpreted_obj);
             strncpy(shell_command, interpreted, command_size - 1);
             shell_command[command_size - 1] = '\0';
             json_object_put(response_obj);
             return 0;
         }
     } else if (strcmp(status, "unsafe") == 0) {
         json_object_put(response_obj);
         return -2; /* Unsafe command */
     } else if (strcmp(status, "unclear") == 0) {
         json_object_put(response_obj);
         return -3; /* Unclear command */
     }
     
     json_object_put(response_obj);
     return -1;
 }
 
 /* Execute command through daemon */
 int ai_execute_command(const char *command, char *output, size_t output_size) {
     if (!command || !output || output_size == 0) {
         return -1;
     }
     
     /* Create JSON request */
     json_object *request = json_object_new_object();
     json_object *action = json_object_new_string("execute");
     json_object *cmd = json_object_new_string(command);
     
     json_object_object_add(request, "action", action);
     json_object_object_add(request, "command", cmd);
     
     const char *request_str = json_object_to_json_string(request);
     
     /* Send request */
     char response[MAX_RESPONSE_SIZE];
     int result = send_request(request_str, response, sizeof(response));
     
     json_object_put(request);
     
     if (result != 0) {
         return -1;
     }
     
     /* Parse response */
     json_object *response_obj = json_tokener_parse(response);
     if (!response_obj) {
         return -1;
     }
     
     json_object *output_obj, *exit_code_obj;
     int exit_code = -1;
     
     if (json_object_object_get_ex(response_obj, "execution_result", &output_obj)) {
         const char *exec_output = json_object_get_string(output_obj);
         strncpy(output, exec_output, output_size - 1);
         output[output_size - 1] = '\0';
     }
     
     if (json_object_object_get_ex(response_obj, "exit_code", &exit_code_obj)) {
         exit_code = json_object_get_int(exit_code_obj);
     }
     
     json_object_put(response_obj);
     return exit_code;
 }
 
 /* Get daemon status */
 int ai_get_status(char *status_info, size_t info_size) {
     if (!status_info || info_size == 0) {
         return -1;
     }
     
     /* Create JSON request */
     json_object *request = json_object_new_object();
     json_object *action = json_object_new_string("status");
     
     json_object_object_add(request, "action", action);
     
     const char *request_str = json_object_to_json_string(request);
     
     /* Send request */
     char response[MAX_RESPONSE_SIZE];
     int result = send_request(request_str, response, sizeof(response));
     
     json_object_put(request);
     
     if (result != 0) {
         return -1;
     }
     
     /* Copy response as-is for now */
     strncpy(status_info, response, info_size - 1);
     status_info[info_size - 1] = '\0';
     
     return 0;
 }
 
 /* Set AI model */
 int ai_set_model(const char *model_name) {
     if (!model_name) {
         return -1;
     }
     
     /* Create JSON request */
     json_object *request = json_object_new_object();
     json_object *action = json_object_new_string("set_model");
     json_object *model = json_object_new_string(model_name);
     
     json_object_object_add(request, "action", action);
     json_object_object_add(request, "model", model);
     
     const char *request_str = json_object_to_json_string(request);
     
     /* Send request */
     char response[MAX_RESPONSE_SIZE];
     int result = send_request(request_str, response, sizeof(response));
     
     json_object_put(request);
     
     if (result != 0) {
         return -1;
     }
     
     /* Parse response for success/failure */
     json_object *response_obj = json_tokener_parse(response);
     if (!response_obj) {
         return -1;
     }
     
     json_object *status_obj;
     int success = 0;
     
     if (json_object_object_get_ex(response_obj, "status", &status_obj)) {
         const char *status = json_object_get_string(status_obj);
         success = (strcmp(status, "success") == 0);
     }
     
     json_object_put(response_obj);
     return success ? 0 : -1;
 }
 
 /* Get current context */
 int ai_get_context(char *context_info, size_t info_size) {
     if (!context_info || info_size == 0) {
         return -1;
     }
     
     /* Create JSON request */
     json_object *request = json_object_new_object();
     json_object *action = json_object_new_string("get_context");
     
     json_object_object_add(request, "action", action);
     
     const char *request_str = json_object_to_json_string(request);
     
     /* Send request */
     char response[MAX_RESPONSE_SIZE];
     int result = send_request(request_str, response, sizeof(response));
     
     json_object_put(request);
     
     if (result != 0) {
         return -1;
     }
     
     /* Copy response */
     strncpy(context_info, response, info_size - 1);
     context_info[info_size - 1] = '\0';
     
     return 0;
 }