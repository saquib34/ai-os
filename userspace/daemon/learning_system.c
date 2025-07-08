/*
 * AI-OS Learning System
 * File: userspace/daemon/learning_system.c
 *
 * Implements feedback loops to improve interpretation over time.
 * Stores user feedback and uses it to adjust model selection and command suggestions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <json-c/json.h>
#include <time.h>
#include <sys/stat.h>

#define FEEDBACK_FILE "/etc/ai-os/feedback.json"
#define MAX_FEEDBACK_ENTRIES 1000

/* Feedback entry structure */
typedef struct {
    char natural_command[512];
    char interpreted_command[512];
    int accepted; // 1 = accepted, 0 = rejected
    char model_used[64];
    time_t timestamp;
} feedback_entry_t;

static feedback_entry_t feedback_db[MAX_FEEDBACK_ENTRIES];
static int feedback_count = 0;
static pthread_mutex_t feedback_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Load feedback from file */
void learning_system_load_feedback() {
    pthread_mutex_lock(&feedback_mutex);
    feedback_count = 0;
    FILE *file = fopen(FEEDBACK_FILE, "r");
    if (!file) {
        fprintf(stderr, "[AI-OS Learning] Could not open feedback file %s for reading\n", FEEDBACK_FILE);
        pthread_mutex_unlock(&feedback_mutex);
        return;
    }
    json_object *root = json_object_from_file(FEEDBACK_FILE);
    if (!root) {
        fprintf(stderr, "[AI-OS Learning] Failed to parse feedback JSON from %s\n", FEEDBACK_FILE);
        fclose(file);
        pthread_mutex_unlock(&feedback_mutex);
        return;
    }
    int len = json_object_array_length(root);
    for (int i = 0; i < len && i < MAX_FEEDBACK_ENTRIES; i++) {
        json_object *entry = json_object_array_get_idx(root, i);
        feedback_entry_t *fb = &feedback_db[feedback_count++];
        memset(fb, 0, sizeof(*fb));
        json_object *nat, *interp, *acc, *model, *ts;
        if (json_object_object_get_ex(entry, "natural_command", &nat))
            strncpy(fb->natural_command, json_object_get_string(nat), sizeof(fb->natural_command)-1);
        if (json_object_object_get_ex(entry, "interpreted_command", &interp))
            strncpy(fb->interpreted_command, json_object_get_string(interp), sizeof(fb->interpreted_command)-1);
        if (json_object_object_get_ex(entry, "accepted", &acc))
            fb->accepted = json_object_get_boolean(acc);
        if (json_object_object_get_ex(entry, "model_used", &model))
            strncpy(fb->model_used, json_object_get_string(model), sizeof(fb->model_used)-1);
        if (json_object_object_get_ex(entry, "timestamp", &ts))
            fb->timestamp = json_object_get_int64(ts);
    }
    if (len >= MAX_FEEDBACK_ENTRIES) {
        fprintf(stderr, "[AI-OS Learning] Warning: feedback truncated to %d entries\n", MAX_FEEDBACK_ENTRIES);
    }
    json_object_put(root);
    fclose(file);
    pthread_mutex_unlock(&feedback_mutex);
}

/* Save feedback to file */
void learning_system_save_feedback() {
    pthread_mutex_lock(&feedback_mutex);
    // Ensure feedback directory exists
    char *dir = strdup(FEEDBACK_FILE);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        struct stat st = {0};
        if (stat(dir, &st) == -1) {
            if (mkdir(dir, 0755) != 0) {
                fprintf(stderr, "[AI-OS Learning] Failed to create feedback directory %s\n", dir);
            }
        }
    }
    free(dir);
    json_object *root = json_object_new_array();
    for (int i = 0; i < feedback_count; i++) {
        feedback_entry_t *fb = &feedback_db[i];
        json_object *entry = json_object_new_object();
        json_object_object_add(entry, "natural_command", json_object_new_string(fb->natural_command));
        json_object_object_add(entry, "interpreted_command", json_object_new_string(fb->interpreted_command));
        json_object_object_add(entry, "accepted", json_object_new_boolean(fb->accepted));
        json_object_object_add(entry, "model_used", json_object_new_string(fb->model_used));
        json_object_object_add(entry, "timestamp", json_object_new_int64(fb->timestamp));
        json_object_array_add(root, entry);
    }
    if (json_object_to_file(FEEDBACK_FILE, root) != 0) {
        fprintf(stderr, "[AI-OS Learning] Failed to save feedback to %s\n", FEEDBACK_FILE);
    }
    json_object_put(root);
    pthread_mutex_unlock(&feedback_mutex);
}

/* Add feedback entry */
void learning_system_add_feedback(const char *natural, const char *interpreted, int accepted, const char *model) {
    pthread_mutex_lock(&feedback_mutex);
    if (feedback_count >= MAX_FEEDBACK_ENTRIES) {
        // Remove oldest
        memmove(&feedback_db[0], &feedback_db[1], sizeof(feedback_entry_t) * (MAX_FEEDBACK_ENTRIES - 1));
        feedback_count = MAX_FEEDBACK_ENTRIES - 1;
        fprintf(stderr, "[AI-OS Learning] Warning: feedback truncated, oldest entry removed\n");
    }
    feedback_entry_t *fb = &feedback_db[feedback_count++];
    strncpy(fb->natural_command, natural, sizeof(fb->natural_command)-1);
    strncpy(fb->interpreted_command, interpreted, sizeof(fb->interpreted_command)-1);
    fb->accepted = accepted;
    strncpy(fb->model_used, model, sizeof(fb->model_used)-1);
    fb->timestamp = time(NULL);
    pthread_mutex_unlock(&feedback_mutex);
    learning_system_save_feedback();
}

/* Suggest better command based on feedback */
int learning_system_suggest(const char *natural, char *suggested, size_t size) {
    pthread_mutex_lock(&feedback_mutex);
    for (int i = feedback_count - 1; i >= 0; i--) {
        feedback_entry_t *fb = &feedback_db[i];
        if (fb->accepted && strcasecmp(fb->natural_command, natural) == 0) {
            strncpy(suggested, fb->interpreted_command, size-1);
            suggested[size-1] = '\0';
            pthread_mutex_unlock(&feedback_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&feedback_mutex);
    return 0;
}

/* Get feedback stats for a model */
int learning_system_model_stats(const char *model, int *accepted, int *rejected) {
    *accepted = 0;
    *rejected = 0;
    pthread_mutex_lock(&feedback_mutex);
    for (int i = 0; i < feedback_count; i++) {
        feedback_entry_t *fb = &feedback_db[i];
        if (strcmp(fb->model_used, model) == 0) {
            if (fb->accepted) (*accepted)++;
            else (*rejected)++;
        }
    }
    pthread_mutex_unlock(&feedback_mutex);
    return 0;
}

/* Initialize learning system */
void learning_system_init() {
    learning_system_load_feedback();
}

/* Cleanup learning system */
void learning_system_cleanup() {
    learning_system_save_feedback();
} 