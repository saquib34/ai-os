/*
 * AI-OS Command Line Client
 * File: userspace/client/ai-client.c
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <getopt.h>
 #include <sys/stat.h>
 #include <stdarg.h>
 #include <time.h>
 #include <pthread.h>
 #include <json-c/json.h>

 /* Include our client library functions */
 extern int ai_client_connect(void);
 extern void ai_client_disconnect(void);
 extern int ai_interpret_command(const char *natural_command, char *shell_command, size_t command_size);
 extern int ai_execute_command(const char *command, char *output, size_t output_size);
 extern int ai_get_status(char *status_info, size_t info_size);
 extern int ai_set_model(const char *model_name);
 extern int ai_get_context(char *context_info, size_t info_size);
 extern int ai_classify_input(const char *input, char *classification, size_t classification_size);
 
 #define MAX_COMMAND_SIZE 4096
 #define MAX_OUTPUT_SIZE 8192
 
 #define AI_CLIENT_CLI_LOG_FILE "/var/log/ai-os/ai_client_cli.log"
 #define AI_CLIENT_CLI_LOG_MAX_SIZE (1024 * 1024) // 1MB
 static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
 static FILE *log_file = NULL;
 static void ai_client_cli_rotate_log() {
     struct stat st;
     if (stat(AI_CLIENT_CLI_LOG_FILE, &st) == 0 && st.st_size > AI_CLIENT_CLI_LOG_MAX_SIZE) {
         char rotated[512];
         snprintf(rotated, sizeof(rotated), "%s.old", AI_CLIENT_CLI_LOG_FILE);
         rename(AI_CLIENT_CLI_LOG_FILE, rotated);
     }
 }
 static void ai_client_cli_log(const char *fmt, ...) {
     pthread_mutex_lock(&log_mutex);
     ai_client_cli_rotate_log();
     if (!log_file) {
         log_file = fopen(AI_CLIENT_CLI_LOG_FILE, "a");
         if (!log_file) log_file = stderr;
     }
     va_list args;
     va_start(args, fmt);
     vfprintf(log_file, fmt, args);
     fflush(log_file);
     va_end(args);
     pthread_mutex_unlock(&log_mutex);
 }
 
 /* Print usage information */
 void print_usage(const char *program_name) {
     printf("AI-OS Command Line Client\n\n");
     printf("Usage: %s [OPTIONS] COMMAND [ARGS...]\n\n", program_name);
     printf("Commands:\n");
     printf("  interpret <text>     Interpret natural language command\n");
     printf("  execute <command>    Execute shell command through daemon\n");
     printf("  status              Show daemon and AI status\n");
     printf("  context             Show current context information\n");
     printf("  model <name>        Set AI model\n");
     printf("  models              List available models\n");
     printf("  interactive         Start interactive mode\n");
     printf("  help                Show this help message\n\n");
     printf("Options:\n");
     printf("  -h, --help          Show help message\n");
     printf("  -v, --verbose       Verbose output\n");
     printf("  -q, --quiet         Quiet mode (minimal output)\n");
     printf("  -j, --json          Output in JSON format\n");
     printf("  -e, --execute       Auto-execute interpreted commands\n\n");
     printf("Examples:\n");
     printf("  %s interpret \"git push and add all files\"\n", program_name);
     printf("  %s execute \"ls -la\"\n", program_name);
     printf("  %s status\n", program_name);
     printf("  %s model phi3:mini\n", program_name);
     printf("  %s interactive\n", program_name);
 }
 
 /* Interactive mode */
 int interactive_mode(int auto_execute, int verbose) {
     char input[MAX_COMMAND_SIZE];
     char command[MAX_COMMAND_SIZE];
     char output[MAX_OUTPUT_SIZE];
     char response;
     
     printf("AI-OS Interactive Mode\n");
     printf("Type 'exit' or 'quit' to leave, 'help' for commands.\n\n");
     
     while (1) {
         printf("ai> ");
         fflush(stdout);
         
         if (!fgets(input, sizeof(input), stdin)) {
             ai_client_cli_log("Error: Failed to read input in interactive mode\n");
             break;
         }
         
         /* Remove trailing newline */
         size_t len = strlen(input);
         if (len > 0 && input[len-1] == '\n') {
             input[len-1] = '\0';
             len--;
         }
         
         if (len == 0) continue;
         
         /* Handle special commands */
         if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
             break;
         }
         
         if (strcmp(input, "help") == 0) {
             printf("Interactive commands:\n");
             printf("  help     - Show this help\n");
             printf("  status   - Show daemon status\n");
             printf("  context  - Show current context\n");
             printf("  exit     - Exit interactive mode\n");
             printf("  <text>   - Interpret natural language command\n\n");
             continue;
         }
         
         if (strcmp(input, "status") == 0) {
             if (ai_get_status(output, sizeof(output)) == 0) {
                 printf("Status: %s\n", output);
             } else {
                 ai_client_cli_log("Error: Failed to get status in interactive mode\n");
                 printf("Error: Failed to get status\n");
             }
             continue;
         }
         
         if (strcmp(input, "context") == 0) {
             if (ai_get_context(output, sizeof(output)) == 0) {
                 printf("Context: %s\n", output);
             } else {
                 ai_client_cli_log("Error: Failed to get context in interactive mode\n");
                 printf("Error: Failed to get context\n");
             }
             continue;
         }
         
         /* Interpret the command */
         int result = ai_interpret_command(input, command, sizeof(command));
         
         switch (result) {
             case 0:
                 printf("Interpreted: %s\n", command);
                 
                 if (auto_execute) {
                     printf("Executing...\n");
                     int exec_result = ai_execute_command(command, output, sizeof(output));
                     if (exec_result == 0) {
                         if (strlen(output) > 0) {
                             printf("%s", output);
                         } else {
                             printf("Command executed successfully.\n");
                         }
                     } else {
                         printf("Execution failed with exit code: %d\n", exec_result);
                         if (strlen(output) > 0) {
                             printf("Output: %s", output);
                         }
                     }
                 } else {
                     printf("Execute this command? [Y/n] ");
                     fflush(stdout);
                     
                     response = getchar();
                     if (response != '\n') {
                         while (getchar() != '\n'); /* Clear input buffer */
                     }
                     
                     if (response != 'n' && response != 'N') {
                         int exec_result = ai_execute_command(command, output, sizeof(output));
                         if (exec_result == 0) {
                             if (strlen(output) > 0) {
                                 printf("%s", output);
                             } else {
                                 printf("Command executed successfully.\n");
                             }
                         } else {
                             printf("Execution failed with exit code: %d\n", exec_result);
                             if (strlen(output) > 0) {
                                 printf("Output: %s", output);
                             }
                         }
                     } else {
                         printf("Command cancelled.\n");
                     }
                 }
                 break;
                 
             case -2:
                 ai_client_cli_log("Error: Command marked as unsafe in interactive mode\n");
                 printf("Error: Command marked as unsafe\n");
                 break;
                 
             case -3:
                 ai_client_cli_log("Error: Command unclear in interactive mode\n");
                 printf("Error: Command unclear, please rephrase\n");
                 break;
                 
             default:
                 ai_client_cli_log("Error: Failed to interpret command in interactive mode\n");
                 printf("Error: Failed to interpret command\n");
                 break;
         }
         
         printf("\n");
     }
     
     printf("Goodbye!\n");
     return 0;
 }
 
 /* Main function */
 int main(int argc, char *argv[]) {
     int verbose = 0;
     int quiet = 0;
     int json_output = 0;
     int auto_execute = 0;
     
     /* Parse command line options */
     static struct option long_options[] = {
         {"help", no_argument, 0, 'h'},
         {"verbose", no_argument, 0, 'v'},
         {"quiet", no_argument, 0, 'q'},
         {"json", no_argument, 0, 'j'},
         {"execute", no_argument, 0, 'e'},
         {0, 0, 0, 0}
     };
     
     int option_index = 0;
     int c;
     
     while ((c = getopt_long(argc, argv, "hvqje", long_options, &option_index)) != -1) {
         switch (c) {
             case 'h':
                 print_usage(argv[0]);
                 return 0;
             case 'v':
                 verbose = 1;
                 break;
             case 'q':
                 quiet = 1;
                 break;
             case 'j':
                 json_output = 1;
                 break;
             case 'e':
                 auto_execute = 1;
                 break;
             case '?':
                 return 1;
             default:
                 abort();
         }
     }
     
     /* Check if we have a command */
     if (optind >= argc) {
         if (!quiet) {
             ai_client_cli_log("Error: No command specified\n");
             print_usage(argv[0]);
         }
         return 1;
     }
     
     /* Connect to daemon */
     if (ai_client_connect() != 0) {
         if (!quiet) {
             ai_client_cli_log("Error: Failed to connect to AI daemon\n");
             ai_client_cli_log("Make sure the daemon is running: sudo systemctl start ai-os\n");
         }
         return 1;
     }
     
     char command[MAX_COMMAND_SIZE];
     char output[MAX_OUTPUT_SIZE];
     int result = 0;
     
     const char *action = argv[optind];
     
     if (strcmp(action, "interpret") == 0) {
         if (optind + 1 >= argc) {
             ai_client_cli_log("Error: No command to interpret\n");
             ai_client_disconnect();
             return 1;
         }
         
         /* Join all remaining arguments as the command */
         command[0] = '\0';
         for (int i = optind + 1; i < argc; i++) {
             if (i > optind + 1) strcat(command, " ");
             strcat(command, argv[i]);
         }
         
         int interpret_result = ai_interpret_command(command, output, sizeof(output));
         
         if (json_output) {
             printf("{\"input\":\"%s\",\"output\":\"%s\",\"status\":%d}\n", 
                    command, output, interpret_result);
         } else {
             switch (interpret_result) {
                 case 0:
                     printf("%s\n", output);
                     break;
                 case -2:
                     if (!quiet) ai_client_cli_log("Error: Command marked as unsafe\n");
                     result = 2;
                     break;
                 case -3:
                     if (!quiet) ai_client_cli_log("Error: Command unclear\n");
                     result = 3;
                     break;
                 default:
                     if (!quiet) ai_client_cli_log("Error: Failed to interpret command\n");
                     result = 1;
                     break;
             }
         }
         
     } else if (strcmp(action, "execute") == 0) {
         if (optind + 1 >= argc) {
             ai_client_cli_log("Error: No command to execute\n");
             ai_client_disconnect();
             return 1;
         }
         
         /* Join all remaining arguments as the command */
         command[0] = '\0';
         for (int i = optind + 1; i < argc; i++) {
             if (i > optind + 1) strcat(command, " ");
             strcat(command, argv[i]);
         }
         
         int exec_result = ai_execute_command(command, output, sizeof(output));
         
         if (json_output) {
             printf("{\"command\":\"%s\",\"output\":\"%s\",\"exit_code\":%d}\n", 
                    command, output, exec_result);
         } else {
             if (strlen(output) > 0) {
                 printf("%s", output);
             }
             if (exec_result != 0 && !quiet) {
                 ai_client_cli_log("Command exited with code: %d\n", exec_result);
             }
         }
         
         result = exec_result;
         
     } else if (strcmp(action, "status") == 0) {
         if (ai_get_status(output, sizeof(output)) == 0) {
             printf("%s\n", output);
         } else {
             if (!quiet) ai_client_cli_log("Error: Failed to get status\n");
             result = 1;
         }
         
     } else if (strcmp(action, "context") == 0) {
         if (ai_get_context(output, sizeof(output)) == 0) {
             printf("%s\n", output);
         } else {
             if (!quiet) ai_client_cli_log("Error: Failed to get context\n");
             result = 1;
         }
         
     } else if (strcmp(action, "model") == 0) {
         if (optind + 1 >= argc) {
             ai_client_cli_log("Error: No model name specified\n");
             ai_client_disconnect();
             return 1;
         }
         
         const char *model_name = argv[optind + 1];
         if (ai_set_model(model_name) == 0) {
             if (!quiet) printf("Model set to: %s\n", model_name);
         } else {
             if (!quiet) ai_client_cli_log("Error: Failed to set model\n");
             result = 1;
         }
         
     } else if (strcmp(action, "classify") == 0) {
         if (optind + 1 >= argc) {
             ai_client_cli_log("Error: No input to classify\n");
             ai_client_disconnect();
             return 1;
         }
         
         /* Join all remaining arguments as the input */
         command[0] = '\0';
         for (int i = optind + 1; i < argc; i++) {
             if (i > optind + 1) strcat(command, " ");
             strcat(command, argv[i]);
         }
         
         /* Send classify request to daemon */
         char classification[256];
         int classify_result = ai_classify_input(command, classification, sizeof(classification));
         
         if (classify_result == 0) {
             printf("%s\n", classification);
         } else {
             if (!quiet) ai_client_cli_log("Error: Failed to classify input\n");
             result = 1;
         }
         
     } else if (strcmp(action, "chat") == 0) {
         if (optind + 1 >= argc) {
             ai_client_cli_log("Error: No input for chat\n");
             ai_client_disconnect();
             return 1;
         }
         
         /* Join all remaining arguments as the input */
         command[0] = '\0';
         for (int i = optind + 1; i < argc; i++) {
             if (i > optind + 1) strcat(command, " ");
             strcat(command, argv[i]);
         }
         
         /* Send chat request to daemon */
         char chat_response[1024];
         int chat_result = ai_interpret_command(command, chat_response, sizeof(chat_response));
         
         if (chat_result == 0) {
             /* Parse JSON response to extract chat_response */
             json_object *response_obj = json_tokener_parse(chat_response);
             if (response_obj) {
                 json_object *chat_response_obj;
                 if (json_object_object_get_ex(response_obj, "chat_response", &chat_response_obj)) {
                     printf("%s\n", json_object_get_string(chat_response_obj));
                 } else {
                     printf("%s\n", chat_response);
                 }
                 json_object_put(response_obj);
             } else {
                 printf("%s\n", chat_response);
             }
         } else {
             if (!quiet) ai_client_cli_log("Error: Failed to get chat response\n");
             result = 1;
         }
         
     } else if (strcmp(action, "interactive") == 0) {
         result = interactive_mode(auto_execute, verbose);
         
     } else if (strcmp(action, "help") == 0) {
         print_usage(argv[0]);
         
     } else {
         /* Try to interpret as natural language command */
         command[0] = '\0';
         for (int i = optind; i < argc; i++) {
             if (i > optind) strcat(command, " ");
             strcat(command, argv[i]);
         }
         
         if (verbose && !quiet) {
             printf("Interpreting: %s\n", command);
         }
         
         int interpret_result = ai_interpret_command(command, output, sizeof(output));
         
         switch (interpret_result) {
             case 0:
                 if (verbose && !quiet) {
                     printf("Interpreted as: %s\n", output);
                 }
                 
                 if (auto_execute) {
                     if (verbose && !quiet) {
                         printf("Auto-executing...\n");
                     }
                     
                     char exec_output[MAX_OUTPUT_SIZE];
                     int exec_result = ai_execute_command(output, exec_output, sizeof(exec_output));
                     
                     if (json_output) {
                         printf("{\"input\":\"%s\",\"interpreted\":\"%s\",\"output\":\"%s\",\"exit_code\":%d}\n", 
                                command, output, exec_output, exec_result);
                     } else {
                         if (strlen(exec_output) > 0) {
                             printf("%s", exec_output);
                         }
                         if (exec_result != 0 && verbose && !quiet) {
                             ai_client_cli_log("Command exited with code: %d\n", exec_result);
                         }
                     }
                     result = exec_result;
                 } else {
                     if (json_output) {
                         printf("{\"input\":\"%s\",\"interpreted\":\"%s\",\"status\":0}\n", 
                                command, output);
                     } else {
                         printf("%s\n", output);
                     }
                 }
                 break;
                 
             case -2:
                 if (json_output) {
                     printf("{\"input\":\"%s\",\"error\":\"unsafe\",\"status\":-2}\n", command);
                 } else {
                     if (!quiet) ai_client_cli_log("Error: Command marked as unsafe\n");
                 }
                 result = 2;
                 break;
                 
             case -3:
                 if (json_output) {
                     printf("{\"input\":\"%s\",\"error\":\"unclear\",\"status\":-3}\n", command);
                 } else {
                     if (!quiet) ai_client_cli_log("Error: Command unclear, please rephrase\n");
                 }
                 result = 3;
                 break;
                 
             default:
                 if (json_output) {
                     printf("{\"input\":\"%s\",\"error\":\"interpretation_failed\",\"status\":-1}\n", command);
                 } else {
                     if (!quiet) {
                         ai_client_cli_log("Error: Failed to interpret command\n");
                         ai_client_cli_log("Available commands: interpret, execute, status, context, model, interactive, help\n");
                     }
                 }
                 result = 1;
                 break;
         }
     }
     
     /* Cleanup */
     ai_client_disconnect();
     pthread_mutex_lock(&log_mutex);
     if (log_file && log_file != stderr) fclose(log_file);
     log_file = NULL;
     pthread_mutex_unlock(&log_mutex);
     return result;
 }