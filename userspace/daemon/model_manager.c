/*
 * Advanced Model Manager for AI-OS
 * File: userspace/daemon/model_manager.c
 * 
 * This component provides intelligent model switching based on task type,
 * performance metrics, and user preferences.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <json-c/json.h>
#include <time.h>
#include <sys/time.h>
#include <curl/curl.h>
#include <errno.h>
#include <sys/stat.h>

/* Logging utility */
static FILE *log_file = NULL;
static void model_manager_log(const char *fmt, ...) {
    va_list args;
    if (!log_file) {
        log_file = fopen("/var/log/ai-os/model_manager.log", "a");
        if (!log_file) log_file = stderr;
    }
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    fflush(log_file);
    va_end(args);
}

/* Model configuration structure */
typedef struct {
    char name[64];
    char description[256];
    char api_url[256];
    int max_tokens;
    float temperature;
    int timeout;
    char task_types[512];  /* Comma-separated list of supported task types */
    float performance_score;
    int success_count;
    int failure_count;
    double avg_response_time;
    int priority;
    int enabled;
} ai_model_config_t;

/* Task type definitions */
#define TASK_TYPE_FILE_OPS     "file_ops"
#define TASK_TYPE_PROCESS_OPS  "process_ops"
#define TASK_TYPE_NETWORK_OPS  "network_ops"
#define TASK_TYPE_SYSTEM_OPS   "system_ops"
#define TASK_TYPE_DEV_OPS      "dev_ops"
#define TASK_TYPE_DATA_OPS     "data_ops"
#define TASK_TYPE_SECURITY_OPS "security_ops"
#define TASK_TYPE_GENERAL      "general"

/* Model registry */
static ai_model_config_t model_registry[] = {
    {
        .name = "codellama:7b-instruct",
        .description = "Code-focused model for development tasks",
        .api_url = "http://localhost:11434/api",
        .max_tokens = 512,
        .temperature = 0.1,
        .timeout = 30,
        .task_types = TASK_TYPE_DEV_OPS "," TASK_TYPE_FILE_OPS "," TASK_TYPE_SYSTEM_OPS,
        .performance_score = 0.85,
        .success_count = 0,
        .failure_count = 0,
        .avg_response_time = 0.0,
        .priority = 1,
        .enabled = 1
    },
    {
        .name = "phi3:mini",
        .description = "Fast general-purpose model",
        .api_url = "http://localhost:11434/api",
        .max_tokens = 256,
        .temperature = 0.2,
        .timeout = 15,
        .task_types = TASK_TYPE_GENERAL "," TASK_TYPE_FILE_OPS "," TASK_TYPE_PROCESS_OPS,
        .performance_score = 0.75,
        .success_count = 0,
        .failure_count = 0,
        .avg_response_time = 0.0,
        .priority = 2,
        .enabled = 1
    },
    {
        .name = "llama3.2:3b",
        .description = "Balanced model for mixed tasks",
        .api_url = "http://localhost:11434/api",
        .max_tokens = 384,
        .temperature = 0.15,
        .timeout = 20,
        .task_types = TASK_TYPE_GENERAL "," TASK_TYPE_NETWORK_OPS "," TASK_TYPE_DATA_OPS,
        .performance_score = 0.80,
        .success_count = 0,
        .failure_count = 0,
        .avg_response_time = 0.0,
        .priority = 3,
        .enabled = 1
    },
    {
        .name = "mistral:7b-instruct",
        .description = "High-quality model for complex tasks",
        .api_url = "http://localhost:11434/api",
        .max_tokens = 1024,
        .temperature = 0.1,
        .timeout = 45,
        .task_types = TASK_TYPE_SECURITY_OPS "," TASK_TYPE_DEV_OPS "," TASK_TYPE_SYSTEM_OPS,
        .performance_score = 0.90,
        .success_count = 0,
        .failure_count = 0,
        .avg_response_time = 0.0,
        .priority = 0,
        .enabled = 1
    }
};

#define MAX_MODELS (sizeof(model_registry) / sizeof(model_registry[0]))

/* Model manager state */
typedef struct {
    ai_model_config_t *current_model;
    pthread_mutex_t model_mutex;
    pthread_mutex_t stats_mutex;
    int auto_switch_enabled;
    int learning_enabled;
    char config_file[256];
    time_t last_switch;
    int switch_cooldown;  /* Minimum seconds between switches */
} model_manager_t;

static model_manager_t g_model_manager = {0};

/* Task classification patterns */
static const char *task_patterns[][2] = {
    /* File operations */
    {TASK_TYPE_FILE_OPS, "file|files|document|folder|directory|path|ls|find|grep|cat|head|tail|cp|mv|rm|mkdir|touch"},
    {TASK_TYPE_FILE_OPS, "show.*file|list.*file|create.*file|delete.*file|move.*file|copy.*file"},
    
    /* Process operations */
    {TASK_TYPE_PROCESS_OPS, "process|processes|ps|kill|pkill|pgrep|top|htop|systemctl|service|daemon"},
    {TASK_TYPE_PROCESS_OPS, "show.*process|list.*process|kill.*process|start.*service|stop.*service"},
    
    /* Network operations */
    {TASK_TYPE_NETWORK_OPS, "network|networking|connection|port|socket|http|https|ftp|ssh|telnet|ping|curl|wget"},
    {TASK_TYPE_NETWORK_OPS, "check.*connection|test.*network|show.*ports|list.*connections"},
    
    /* System operations */
    {TASK_TYPE_SYSTEM_OPS, "system|hardware|cpu|memory|ram|disk|storage|performance|monitor|resource"},
    {TASK_TYPE_SYSTEM_OPS, "show.*memory|check.*disk|monitor.*system|system.*info"},
    
    /* Development operations */
    {TASK_TYPE_DEV_OPS, "code|coding|development|programming|compile|build|deploy|git|github|repository"},
    {TASK_TYPE_DEV_OPS, "git.*push|git.*pull|git.*commit|build.*project|deploy.*application"},
    
    /* Data operations */
    {TASK_TYPE_DATA_OPS, "data|database|db|sql|nosql|query|search|filter|sort|export|import"},
    {TASK_TYPE_DATA_OPS, "search.*data|query.*database|export.*data|import.*data"},
    
    /* Security operations */
    {TASK_TYPE_SECURITY_OPS, "security|permission|access|authentication|authorization|login|user|group|sudo"},
    {TASK_TYPE_SECURITY_OPS, "check.*permissions|set.*permissions|security.*scan|user.*management"},
    
    {NULL, NULL}
};

/* Initialize model manager */
int model_manager_init(const char *config_file) {
    if (pthread_mutex_init(&g_model_manager.model_mutex, NULL) != 0) {
        model_manager_log("Model Manager: Failed to init model_mutex: %s\n", strerror(errno));
        return -1;
    }
    if (pthread_mutex_init(&g_model_manager.stats_mutex, NULL) != 0) {
        model_manager_log("Model Manager: Failed to init stats_mutex: %s\n", strerror(errno));
        pthread_mutex_destroy(&g_model_manager.model_mutex);
        return -1;
    }
    
    g_model_manager.auto_switch_enabled = 1;
    g_model_manager.learning_enabled = 1;
    g_model_manager.switch_cooldown = 300; /* 5 minutes */
    g_model_manager.last_switch = 0;
    
    if (config_file) {
        strncpy(g_model_manager.config_file, config_file, sizeof(g_model_manager.config_file) - 1);
    } else {
        strcpy(g_model_manager.config_file, "/etc/ai-os/models.json");
    }
    
    /* Set default model */
    g_model_manager.current_model = &model_registry[0];
    
    /* Load configuration */
    model_manager_load_config();
    
    model_manager_log("Model Manager: Initialized with %zu models\n", MAX_MODELS);
    return 0;
}

/* Classify task type based on command */
static const char *classify_task_type(const char *command) {
    if (!command) return TASK_TYPE_GENERAL;
    
    char *lower_command = strdup(command);
    if (!lower_command) return TASK_TYPE_GENERAL;
    
    /* Convert to lowercase */
    for (int i = 0; lower_command[i]; i++) {
        lower_command[i] = tolower(lower_command[i]);
    }
    
    /* Count matches for each task type */
    int task_scores[8] = {0}; /* Assuming 8 task types */
    const char *task_types[] = {
        TASK_TYPE_FILE_OPS, TASK_TYPE_PROCESS_OPS, TASK_TYPE_NETWORK_OPS,
        TASK_TYPE_SYSTEM_OPS, TASK_TYPE_DEV_OPS, TASK_TYPE_DATA_OPS,
        TASK_TYPE_SECURITY_OPS, TASK_TYPE_GENERAL
    };
    
    /* Check patterns */
    for (int i = 0; task_patterns[i][0]; i++) {
        const char *task_type = task_patterns[i][0];
        const char *pattern = task_patterns[i][1];
        
        // TODO: Use regex for robust pattern matching
        if (strstr(lower_command, pattern)) {
            for (int j = 0; j < 8; j++) {
                if (strcmp(task_types[j], task_type) == 0) {
                    task_scores[j]++;
                    break;
                }
            }
        }
    }
    
    /* Find task type with highest score */
    int max_score = 0;
    int best_task = 7; /* Default to GENERAL */
    
    for (int i = 0; i < 7; i++) { /* Skip GENERAL in loop */
        if (task_scores[i] > max_score) {
            max_score = task_scores[i];
            best_task = i;
        }
    }
    
    free(lower_command);
    return task_types[best_task];
}

/* Check if model supports task type */
static int model_supports_task(ai_model_config_t *model, const char *task_type) {
    if (!model || !task_type) return 0;
    
    char *task_types = strdup(model->task_types);
    if (!task_types) return 0;
    
    char *token = strtok(task_types, ",");
    while (token) {
        /* Remove leading/trailing spaces */
        while (*token == ' ') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ') end--;
        *(end + 1) = '\0';
        
        if (strcmp(token, task_type) == 0) {
            free(task_types);
            return 1;
        }
        token = strtok(NULL, ",");
    }
    
    free(task_types);
    return 0;
}

/* Select best model for task */
static ai_model_config_t *select_best_model(const char *task_type) {
    ai_model_config_t *best_model = NULL;
    float best_score = -1.0;
    
    for (size_t i = 0; i < MAX_MODELS; i++) {
        ai_model_config_t *model = &model_registry[i];
        
        if (!model->enabled) continue;
        
        if (!model_supports_task(model, task_type)) continue;
        
        /* Calculate composite score */
        float score = model->performance_score;
        
        /* Adjust for response time (faster is better) */
        if (model->avg_response_time > 0) {
            score -= (model->avg_response_time / 10.0); /* Normalize to 0-1 range */
        }
        
        /* Adjust for success rate */
        int total_requests = model->success_count + model->failure_count;
        if (total_requests > 0) {
            float success_rate = (float)model->success_count / total_requests;
            score = score * 0.7 + success_rate * 0.3;
        }
        
        /* Priority boost */
        score += (10 - model->priority) * 0.01;
        
        if (score > best_score) {
            best_score = score;
            best_model = model;
        }
    }
    
    return best_model ? best_model : &model_registry[0]; /* Fallback to first model */
}

/* Switch to appropriate model for task */
int model_manager_select_model(const char *command) {
    if (!g_model_manager.auto_switch_enabled) {
        return 0;
    }
    
    /* Check cooldown */
    time_t now = time(NULL);
    if (now - g_model_manager.last_switch < g_model_manager.switch_cooldown) {
        return 0;
    }
    
    const char *task_type = classify_task_type(command);
    ai_model_config_t *best_model = select_best_model(task_type);
    
    pthread_mutex_lock(&g_model_manager.model_mutex);
    
    if (best_model != g_model_manager.current_model) {
        model_manager_log("Model Manager: Switching from %s to %s for task type: %s\n",
               g_model_manager.current_model->name, best_model->name, task_type);
        
        g_model_manager.current_model = best_model;
        g_model_manager.last_switch = now;
        
        pthread_mutex_unlock(&g_model_manager.model_mutex);
        return 1; /* Model switched */
    }
    
    pthread_mutex_unlock(&g_model_manager.model_mutex);
    return 0; /* No switch needed */
}

/* Get current model */
ai_model_config_t *model_manager_get_current_model(void) {
    pthread_mutex_lock(&g_model_manager.model_mutex);
    ai_model_config_t *model = g_model_manager.current_model;
    pthread_mutex_unlock(&g_model_manager.model_mutex);
    return model;
}

/* Update model performance statistics */
void model_manager_update_stats(const char *model_name, int success, double response_time) {
    pthread_mutex_lock(&g_model_manager.stats_mutex);
    
    for (size_t i = 0; i < MAX_MODELS; i++) {
        if (strcmp(model_registry[i].name, model_name) == 0) {
            ai_model_config_t *model = &model_registry[i];
            
            if (success) {
                model->success_count++;
            } else {
                model->failure_count++;
            }
            
            /* Update average response time */
            int total_requests = model->success_count + model->failure_count;
            if (total_requests == 1) {
                model->avg_response_time = response_time;
            } else {
                model->avg_response_time = (model->avg_response_time * (total_requests - 1) + response_time) / total_requests;
            }
            
            /* Update performance score */
            if (total_requests >= 10) {
                float success_rate = (float)model->success_count / total_requests;
                model->performance_score = success_rate * 0.8 + (1.0 - model->avg_response_time / 30.0) * 0.2;
            }
            
            break;
        }
    }
    
    pthread_mutex_unlock(&g_model_manager.stats_mutex);
}

/* List available models */
int model_manager_list_models(char *output, size_t output_size) {
    json_object *root = json_object_new_array();
    
    for (size_t i = 0; i < MAX_MODELS; i++) {
        ai_model_config_t *model = &model_registry[i];
        
        json_object *model_obj = json_object_new_object();
        json_object_object_add(model_obj, "name", json_object_new_string(model->name));
        json_object_object_add(model_obj, "description", json_object_new_string(model->description));
        json_object_object_add(model_obj, "enabled", json_object_new_boolean(model->enabled));
        json_object_object_add(model_obj, "performance_score", json_object_new_double(model->performance_score));
        json_object_object_add(model_obj, "success_count", json_object_new_int(model->success_count));
        json_object_object_add(model_obj, "failure_count", json_object_new_int(model->failure_count));
        json_object_object_add(model_obj, "avg_response_time", json_object_new_double(model->avg_response_time));
        json_object_object_add(model_obj, "priority", json_object_new_int(model->priority));
        json_object_object_add(model_obj, "task_types", json_object_new_string(model->task_types));
        
        json_object_array_add(root, model_obj);
    }
    
    const char *json_str = json_object_to_json_string(root);
    strncpy(output, json_str, output_size - 1);
    output[output_size - 1] = '\0';
    
    json_object_put(root);
    return 0;
}

/* Set model manually */
int model_manager_set_model(const char *model_name) {
    pthread_mutex_lock(&g_model_manager.model_mutex);
    
    for (size_t i = 0; i < MAX_MODELS; i++) {
        if (strcmp(model_registry[i].name, model_name) == 0) {
            if (model_registry[i].enabled) {
                g_model_manager.current_model = &model_registry[i];
                g_model_manager.last_switch = time(NULL);
                model_manager_log("Model Manager: Manually switched to %s\n", model_name);
                pthread_mutex_unlock(&g_model_manager.model_mutex);
                return 0;
            } else {
                pthread_mutex_unlock(&g_model_manager.model_mutex);
                return -1; /* Model disabled */
            }
        }
    }
    
    pthread_mutex_unlock(&g_model_manager.model_mutex);
    return -2; /* Model not found */
}

/* Enable/disable auto-switching */
void model_manager_set_auto_switch(int enabled) {
    g_model_manager.auto_switch_enabled = enabled;
    model_manager_log("Model Manager: Auto-switching %s\n", enabled ? "enabled" : "disabled");
}

/* Enable/disable learning */
void model_manager_set_learning(int enabled) {
    g_model_manager.learning_enabled = enabled;
    model_manager_log("Model Manager: Learning %s\n", enabled ? "enabled" : "disabled");
}

/* Load configuration from file */
int model_manager_load_config(void) {
    FILE *file = fopen(g_model_manager.config_file, "r");
    if (!file) {
        model_manager_log("Model Manager: No config file found, using defaults\n");
        return 0;
    }
    
    json_object *root = json_object_from_file(g_model_manager.config_file);
    if (!root) {
        fclose(file);
        model_manager_log("Model Manager: Failed to parse config file\n");
        return -1;
    }
    
    /* Parse configuration */
    json_object *models_array;
    if (json_object_object_get_ex(root, "models", &models_array)) {
        int array_len = json_object_array_length(models_array);
        
        for (int i = 0; i < array_len && i < MAX_MODELS; i++) {
            json_object *model_obj = json_object_array_get_idx(models_array, i);
            
            json_object *name_obj, *enabled_obj, *priority_obj;
            if (json_object_object_get_ex(model_obj, "name", &name_obj) &&
                json_object_object_get_ex(model_obj, "enabled", &enabled_obj)) {
                
                const char *name = json_object_get_string(name_obj);
                
                /* Find matching model in registry */
                for (size_t j = 0; j < MAX_MODELS; j++) {
                    if (strcmp(model_registry[j].name, name) == 0) {
                        model_registry[j].enabled = json_object_get_boolean(enabled_obj);
                        
                        if (json_object_object_get_ex(model_obj, "priority", &priority_obj)) {
                            model_registry[j].priority = json_object_get_int(priority_obj);
                        }
                        break;
                    }
                }
            }
        }
    }
    
    json_object_put(root);
    fclose(file);
    return 0;
}

/* Save configuration to file */
int model_manager_save_config(void) {
    json_object *root = json_object_new_object();
    json_object *models_array = json_object_new_array();
    
    for (size_t i = 0; i < MAX_MODELS; i++) {
        json_object *model_obj = json_object_new_object();
        json_object_object_add(model_obj, "name", json_object_new_string(model_registry[i].name));
        json_object_object_add(model_obj, "enabled", json_object_new_boolean(model_registry[i].enabled));
        json_object_object_add(model_obj, "priority", json_object_new_int(model_registry[i].priority));
        json_object_object_add(model_obj, "performance_score", json_object_new_double(model_registry[i].performance_score));
        json_object_object_add(model_obj, "success_count", json_object_new_int(model_registry[i].success_count));
        json_object_object_add(model_obj, "failure_count", json_object_new_int(model_registry[i].failure_count));
        json_object_object_add(model_obj, "avg_response_time", json_object_new_double(model_registry[i].avg_response_time));
        
        json_object_array_add(models_array, model_obj);
    }
    
    json_object_object_add(root, "models", models_array);
    json_object_object_add(root, "auto_switch_enabled", json_object_new_boolean(g_model_manager.auto_switch_enabled));
    json_object_object_add(root, "learning_enabled", json_object_new_boolean(g_model_manager.learning_enabled));
    json_object_object_add(root, "switch_cooldown", json_object_new_int(g_model_manager.switch_cooldown));
    
    /* Create directory if it doesn't exist */
    char *dir = strdup(g_model_manager.config_file);
    if (!dir) {
        model_manager_log("Model Manager: Failed to allocate memory for config dir\n");
        json_object_put(root);
        return -1;
    }
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        struct stat st = {0};
        if (stat(dir, &st) == -1) {
            if (mkdir(dir, 0755) == -1 && errno != EEXIST) {
                model_manager_log("Model Manager: Failed to create config dir %s: %s\n", dir, strerror(errno));
                free(dir);
                json_object_put(root);
                return -1;
            }
        }
    }
    free(dir);
    
    /* Save to file */
    if (json_object_to_file(g_model_manager.config_file, root) != 0) {
        model_manager_log("Model Manager: Failed to save configuration\n");
        json_object_put(root);
        return -1;
    }
    
    json_object_put(root);
    return 0;
}

/* Get model statistics */
int model_manager_get_stats(char *stats_json, size_t stats_size) {
    json_object *root = json_object_new_object();
    
    /* Current model info */
    ai_model_config_t *current = model_manager_get_current_model();
    json_object *current_obj = json_object_new_object();
    json_object_object_add(current_obj, "name", json_object_new_string(current->name));
    json_object_object_add(current_obj, "description", json_object_new_string(current->description));
    json_object_object_add(current_obj, "performance_score", json_object_new_double(current->performance_score));
    json_object_object_add(current_obj, "avg_response_time", json_object_new_double(current->avg_response_time));
    
    json_object_object_add(root, "current_model", current_obj);
    
    /* Auto-switching info */
    json_object_object_add(root, "auto_switch_enabled", json_object_new_boolean(g_model_manager.auto_switch_enabled));
    json_object_object_add(root, "learning_enabled", json_object_new_boolean(g_model_manager.learning_enabled));
    json_object_object_add(root, "last_switch", json_object_new_int64(g_model_manager.last_switch));
    
    /* All models summary */
    json_object *models_summary = json_object_new_array();
    for (size_t i = 0; i < MAX_MODELS; i++) {
        json_object *summary = json_object_new_object();
        json_object_object_add(summary, "name", json_object_new_string(model_registry[i].name));
        json_object_object_add(summary, "enabled", json_object_new_boolean(model_registry[i].enabled));
        json_object_object_add(summary, "performance_score", json_object_new_double(model_registry[i].performance_score));
        json_object_object_add(summary, "total_requests", json_object_new_int(model_registry[i].success_count + model_registry[i].failure_count));
        json_object_object_add(summary, "success_rate", json_object_new_double(
            (model_registry[i].success_count + model_registry[i].failure_count) > 0 ?
            (double)model_registry[i].success_count / (model_registry[i].success_count + model_registry[i].failure_count) : 0.0
        ));
        
        json_object_array_add(models_summary, summary);
    }
    json_object_object_add(root, "models_summary", models_summary);
    
    const char *json_str = json_object_to_json_string(root);
    strncpy(stats_json, json_str, stats_size - 1);
    stats_json[stats_size - 1] = '\0';
    
    json_object_put(root);
    return 0;
}

/* Cleanup model manager */
void model_manager_cleanup(void) {
    model_manager_save_config();
    pthread_mutex_destroy(&g_model_manager.model_mutex);
    pthread_mutex_destroy(&g_model_manager.stats_mutex);
    if (log_file && log_file != stderr) fclose(log_file);
    log_file = NULL;
    model_manager_log("Model Manager: Cleaned up\n");
} 