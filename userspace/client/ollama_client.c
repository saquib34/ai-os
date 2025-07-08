/*
 * Ollama Client Library for AI-OS
 * File: userspace/client/ollama_client.c
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <curl/curl.h>
 #include <json-c/json.h>
 #include <unistd.h>
 #include <pthread.h>
 #include <locale.h>
 #include <langinfo.h>
 #include <sys/utsname.h>
 #include <sys/stat.h>
 #include <stdarg.h>
 #include <time.h>
 #include "../ai_os_common.h"
 
 #define OLLAMA_CLIENT_LOG_FILE "/var/log/ai-os/ollama_client.log"
 #define OLLAMA_CLIENT_LOG_MAX_SIZE (1024 * 1024) // 1MB

 // Function to get Linux distribution and config
static void get_linux_distribution(char *distro, size_t size, char *config, size_t config_size) {
    FILE *f = fopen("/etc/os-release", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                char *val = strchr(line, '=');
                if (val) {
                    val++;
                    if (*val == '"') val++;
                    char *end = strchr(val, '"');
                    if (end) *end = '\0';
                    strncpy(distro, val, size-1);
                    distro[size-1] = '\0';
                }
            }
        }
        fclose(f);
    } else {
        strncpy(distro, "Unknown Linux", size-1);
        distro[size-1] = '\0';
    }
    // Get kernel version and architecture
    struct utsname uts;
    if (uname(&uts) == 0) {
        snprintf(config, config_size, "Kernel: %s, Arch: %s", uts.release, uts.machine);
    } else {
        strncpy(config, "Unknown config", config_size-1);
        config[config_size-1] = '\0';
    }
}
 
 #define OLLAMA_API_URL "http://localhost:11434/api"
 #define MAX_RESPONSE_SIZE 8192
 #define MAX_PROMPT_SIZE 4096
 
 /* Response structure for HTTP requests */
 struct ollama_response {
     char *data;
     size_t size;
     size_t capacity;
 };
 
 /* Ollama client configuration */
 typedef struct {
     char model_name[64];
     char api_url[256];
     int timeout;
     int max_tokens;
     float temperature;
     pthread_mutex_t mutex;
     CURL *curl_handle;
 } ollama_client_t;
 
 /* Global client instance */
 static ollama_client_t g_client = {0};
 
 /* Callback function to write HTTP response data */
 static size_t write_callback(void *contents, size_t size, size_t nmemb, struct ollama_response *response) {
     size_t real_size = size * nmemb;
     
     /* Ensure we have enough capacity */
     if (response->size + real_size >= response->capacity) {
         response->capacity = (response->size + real_size) * 2;
         response->data = realloc(response->data, response->capacity);
         if (!response->data) {
             return 0; /* Out of memory */
         }
     }
     
     memcpy(response->data + response->size, contents, real_size);
     response->size += real_size;
     response->data[response->size] = '\0';
     
     return real_size;
 }
 
 static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Log rotation utility */
static void ollama_client_rotate_log() {
    struct stat st;
    if (stat(OLLAMA_CLIENT_LOG_FILE, &st) == 0 && st.st_size > OLLAMA_CLIENT_LOG_MAX_SIZE) {
        char rotated[512];
        snprintf(rotated, sizeof(rotated), "%s.old", OLLAMA_CLIENT_LOG_FILE);
        rename(OLLAMA_CLIENT_LOG_FILE, rotated);
    }
}

/* Logging utility with mutex and rotation */
static FILE *log_file = NULL;
static void ollama_client_log(const char *fmt, ...) {
    pthread_mutex_lock(&log_mutex);
    ollama_client_rotate_log();
    if (!log_file) {
        log_file = fopen(OLLAMA_CLIENT_LOG_FILE, "a");
        if (!log_file) log_file = stderr;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    fflush(log_file);
    va_end(args);
    pthread_mutex_unlock(&log_mutex);
}

 /* Initialize Ollama client */
 int ollama_client_init(const char *model_name, const char *api_url) {
     if (pthread_mutex_init(&g_client.mutex, NULL) != 0) {
         ollama_client_log("Ollama Client: Failed to initialize mutex\n");
         return -1;
     }
     
     /* Set default values */
     strncpy(g_client.model_name, model_name ? model_name : "codellama:7b-instruct", sizeof(g_client.model_name) - 1);
     strncpy(g_client.api_url, api_url ? api_url : OLLAMA_API_URL, sizeof(g_client.api_url) - 1);
     g_client.timeout = 30;
     g_client.max_tokens = 512;
     g_client.temperature = 0.1;
     
     /* Initialize CURL */
     curl_global_init(CURL_GLOBAL_DEFAULT);
     g_client.curl_handle = curl_easy_init();
     
     if (!g_client.curl_handle) {
         ollama_client_log("Ollama Client: Failed to initialize CURL\n");
         return -1;
     }
     
     /* Set basic CURL options */
     curl_easy_setopt(g_client.curl_handle, CURLOPT_TIMEOUT, g_client.timeout);
     curl_easy_setopt(g_client.curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
     
     ollama_client_log("Ollama client initialized with model: %s\n", g_client.model_name);
     return 0;
 }
 
 /* Add a simple language detection stub (could be replaced with a real library) */
static const char *detect_language(const char *text) {
    // For demo: if contains non-ASCII, assume Spanish; else English
    for (const char *p = text; *p; ++p) {
        if ((unsigned char)*p > 127) return "Spanish";
    }
    return "English";
}
 
 /* Create system prompt for command interpretation */
 static char *create_system_prompt(const char *context, const char *language) {
     static char system_prompt[4096];
     char distro[128] = "", config[128] = "";
     get_linux_distribution(distro, sizeof(distro), config, sizeof(config));
     snprintf(system_prompt, sizeof(system_prompt),
         "You are an AI assistant that translates natural language commands into Linux shell commands.\n"
         "Input language: %s\n"
         "Linux distribution: %s\n"
         "System configuration: %s\n"
         "Rules:\n"
         "1. Only output the shell command, no explanations\n"
         "2. If unsafe, output 'UNSAFE_COMMAND'\n"
         "3. If unclear, output 'UNCLEAR_COMMAND'\n"
         "4. Consider the context: %s\n"
         "5. Reply in the same language as the input\n\n"
         "Examples:\n"
         "Input: 'git push and add all files'\n"
         "Output: git add . && git push\n\n"
         "Input: 'instala el paquete python numpy'\n"
         "Output: pip install numpy\n\n",
         language ? language : "English",
         distro,
         config,
         context ? context : "Current directory, standard user permissions");
     return system_prompt;
 }
 
 /* Send request to Ollama API */
 static int send_ollama_request(const char *prompt, const char *context, char *response, size_t response_size) {
     struct ollama_response http_response = {0};
     http_response.data = malloc(MAX_RESPONSE_SIZE);
     http_response.capacity = MAX_RESPONSE_SIZE;
     
     if (!http_response.data) {
         return -1;
     }
     
     /* Create JSON request */
     json_object *request = json_object_new_object();
     json_object *model = json_object_new_string(g_client.model_name);
     const char *language = detect_language(prompt);
     json_object *system_prompt = json_object_new_string(create_system_prompt(context, language));
     json_object *user_prompt = json_object_new_string(prompt);
     json_object *stream = json_object_new_boolean(0);
     json_object *options = json_object_new_object();
     json_object *temperature = json_object_new_double(g_client.temperature);
     json_object *num_predict = json_object_new_int(g_client.max_tokens);
     
     json_object_object_add(options, "temperature", temperature);
     json_object_object_add(options, "num_predict", num_predict);
     
     json_object_object_add(request, "model", model);
     json_object_object_add(request, "system", system_prompt);
     json_object_object_add(request, "prompt", user_prompt);
     json_object_object_add(request, "stream", stream);
     json_object_object_add(request, "options", options);
     
     const char *json_string = json_object_to_json_string(request);
     
     /* Setup CURL for POST request */
     char url[512];
     snprintf(url, sizeof(url), "%s/generate", g_client.api_url);
     
     struct curl_slist *headers = NULL;
     headers = curl_slist_append(headers, "Content-Type: application/json");
     
     curl_easy_setopt(g_client.curl_handle, CURLOPT_URL, url);
     curl_easy_setopt(g_client.curl_handle, CURLOPT_POSTFIELDS, json_string);
     curl_easy_setopt(g_client.curl_handle, CURLOPT_HTTPHEADER, headers);
     curl_easy_setopt(g_client.curl_handle, CURLOPT_WRITEDATA, &http_response);
     curl_easy_setopt(g_client.curl_handle, CURLOPT_TIMEOUT, 15L); // HTTP timeout
     int max_attempts = 5;
     int attempt = 0;
     int backoff = 1;
     CURLcode res = CURLE_OK;
     while (attempt < max_attempts) {
         res = curl_easy_perform(g_client.curl_handle);
         if (res == CURLE_OK) break;
         ollama_client_log("Ollama Client: CURL error (attempt %d): %s\n", attempt + 1, curl_easy_strerror(res));
         sleep(backoff);
         backoff *= 2;
         if (backoff > 16) backoff = 16;
         attempt++;
     }
     
     /* Cleanup */
     curl_slist_free_all(headers);
     json_object_put(request);
     
     if (res != CURLE_OK) {
         ollama_client_log("Ollama Client: CURL error after %d attempts: %s\n", attempt, curl_easy_strerror(res));
         free(http_response.data);
         return -1;
     }
     
     /* Parse response */
     json_object *response_obj = json_tokener_parse(http_response.data);
     if (!response_obj) {
         ollama_client_log("Ollama Client: Failed to parse JSON response\n");
         free(http_response.data);
         return -1;
     }
     
     json_object *response_text;
     if (json_object_object_get_ex(response_obj, "response", &response_text)) {
         const char *text = json_object_get_string(response_text);
         strncpy(response, text, response_size - 1);
         response[response_size - 1] = '\0';
         
         /* Remove trailing newlines */
         size_t len = strlen(response);
         while (len > 0 && (response[len-1] == '\n' || response[len-1] == '\r')) {
             response[--len] = '\0';
         }
     } else {
         strncpy(response, "ERROR: No response from model", response_size - 1);
     }
     
     json_object_put(response_obj);
     free(http_response.data);
     
     return 0;
 }
 
 /* Main interpretation function */
 int ollama_interpret_command(const char *natural_command, const char *context, 
                             char *shell_command, size_t command_size) {
     if (!natural_command || !shell_command || command_size == 0) {
         return -1;
     }
     
     struct timespec mutex_timeout;
     clock_gettime(CLOCK_REALTIME, &mutex_timeout);
     mutex_timeout.tv_sec += 5; // 5 second timeout for mutex
     if (pthread_mutex_timedlock(&g_client.mutex, &mutex_timeout) != 0) {
         ollama_client_log("Ollama Client: Timed out waiting for mutex in interpret_command\n");
         return -1;
     }
     
     ollama_client_log("AI-OS: Interpreting '%s' with context '%s'\n", 
            natural_command, context ? context : "none");
     
     int result = send_ollama_request(natural_command, context, shell_command, command_size);
     
     pthread_mutex_unlock(&g_client.mutex);
     
     if (result == 0) {
         ollama_client_log("AI-OS: Interpreted as '%s'\n", shell_command);
         
         /* Check for safety markers */
         if (strstr(shell_command, "UNSAFE_COMMAND")) {
             return -2; /* Unsafe command */
         }
         if (strstr(shell_command, "UNCLEAR_COMMAND")) {
             return -3; /* Unclear command */
         }
     }
     
     return result;
 }
 
 /* Check if Ollama is running */
 int ollama_check_status(void) {
     struct ollama_response response = {0};
     response.data = malloc(1024);
     response.capacity = 1024;
     
     if (!response.data) {
         return -1;
     }
     
     char url[512];
     snprintf(url, sizeof(url), "%s/tags", g_client.api_url);
     
     curl_easy_setopt(g_client.curl_handle, CURLOPT_URL, url);
     curl_easy_setopt(g_client.curl_handle, CURLOPT_HTTPGET, 1L);
     curl_easy_setopt(g_client.curl_handle, CURLOPT_WRITEDATA, &response);
     
     CURLcode res = curl_easy_perform(g_client.curl_handle);
     
     free(response.data);
     
     if (res == CURLE_OK) {
         long response_code;
         curl_easy_getinfo(g_client.curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
         return (response_code == 200) ? 0 : -1;
     }
     
     return -1;
 }
 
 /* Get available models */
 int ollama_list_models(char *models_list, size_t list_size) {
     struct ollama_response response = {0};
     response.data = malloc(MAX_RESPONSE_SIZE);
     response.capacity = MAX_RESPONSE_SIZE;
     
     if (!response.data) {
         return -1;
     }
     
     char url[512];
     snprintf(url, sizeof(url), "%s/tags", g_client.api_url);
     
     curl_easy_setopt(g_client.curl_handle, CURLOPT_URL, url);
     curl_easy_setopt(g_client.curl_handle, CURLOPT_HTTPGET, 1L);
     curl_easy_setopt(g_client.curl_handle, CURLOPT_WRITEDATA, &response);
     
     CURLcode res = curl_easy_perform(g_client.curl_handle);
     
     if (res != CURLE_OK) {
         free(response.data);
         return -1;
     }
     
     /* Parse and format model list */
     json_object *response_obj = json_tokener_parse(response.data);
     if (response_obj) {
         json_object *models_array;
         if (json_object_object_get_ex(response_obj, "models", &models_array)) {
             int array_len = json_object_array_length(models_array);
             models_list[0] = '\0';
             
             for (int i = 0; i < array_len; i++) {
                 json_object *model = json_object_array_get_idx(models_array, i);
                 json_object *name;
                 if (json_object_object_get_ex(model, "name", &name)) {
                     strncat(models_list, json_object_get_string(name), list_size - strlen(models_list) - 1);
                     if (i < array_len - 1) {
                         strncat(models_list, ", ", list_size - strlen(models_list) - 1);
                     }
                 }
             }
         }
         json_object_put(response_obj);
     }
     
     free(response.data);
     return 0;
 }
 
 /* Set model configuration */
 int ollama_set_model(const char *model_name) {
     if (!model_name) return -1;
     
     struct timespec mutex_timeout;
     clock_gettime(CLOCK_REALTIME, &mutex_timeout);
     mutex_timeout.tv_sec += 5;
     if (pthread_mutex_timedlock(&g_client.mutex, &mutex_timeout) != 0) {
         ollama_client_log("Ollama Client: Timed out waiting for mutex in set_model\n");
         return -1;
     }
     
     strncpy(g_client.model_name, model_name, sizeof(g_client.model_name) - 1);
     pthread_mutex_unlock(&g_client.mutex);
     
     ollama_client_log("AI-OS: Switched to model '%s'\n", model_name);
     return 0;
 }
 
 /* Cleanup */
 void ollama_client_cleanup(void) {
     if (g_client.curl_handle) {
         curl_easy_cleanup(g_client.curl_handle);
     }
     curl_global_cleanup();
     pthread_mutex_destroy(&g_client.mutex);
     ollama_client_log("AI-OS: Ollama client cleaned up\n");
     pthread_mutex_lock(&log_mutex);
     if (log_file && log_file != stderr) fclose(log_file);
     log_file = NULL;
     pthread_mutex_unlock(&log_mutex);
 }