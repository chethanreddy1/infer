#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include "cJSON.h"

// Constants
static const char *SYSTEM_PROMPT = 
    "You are a CLI tool. Output plain text only. No yapping. Keep the output concise. "

    "STRICTLY DO NOT USE MARKDOWNS. NO asterisks. NO backticks. NO formatting. "

    "Just plain readable sentences."; 

struct response {
    char *data;
    size_t size;
};

// Callback to capture HTTP response
static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t realsize = size * nmemb;
    struct response *mem = (struct response *)userdata;
    
    char *ptr_realloc = realloc(mem->data, mem->size + realsize + 1);
    if(!ptr_realloc) return 0; // OOM

    mem->data = ptr_realloc;
    memcpy(&(mem->data[mem->size]), ptr, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    return realsize;
}

// Helper: Read stdin if piped
static char* read_stdin() {
    if (isatty(fileno(stdin))) return NULL; 

    size_t size = 4096, len = 0;
    char *buf = malloc(size);
    int c;
    while (buf && (c = getchar()) != EOF) {
        buf[len++] = (char)c;
        if (len >= size - 1) {
            size *= 2;
            char *tmp = realloc(buf, size);
            if (!tmp) { free(buf); return NULL; }
            buf = tmp;
        }
    }
    if (buf) buf[len] = 0;
    return buf;
}

// Helper: Construct JSON Payload
static char* build_payload(const char *model, const char *prompt, const char *context) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model ? model : "gpt-3.5-turbo");
    cJSON_AddFalseToObject(root, "stream");
    
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    
    // System Msg
    cJSON *sys = cJSON_CreateObject();
    cJSON_AddStringToObject(sys, "role", "system");
    cJSON_AddStringToObject(sys, "content", SYSTEM_PROMPT);
    cJSON_AddItemToArray(messages, sys);

    // User Msg
    cJSON *usr = cJSON_CreateObject();
    cJSON_AddStringToObject(usr, "role", "user");
    
    if (context) {
        // Combine Prompt + Context
        size_t len = strlen(prompt) + strlen(context) + 50;
        char *full = malloc(len);
        if (full) {
            snprintf(full, len, "%s\n\nContext:\n%s", prompt, context);
            cJSON_AddStringToObject(usr, "content", full);
            free(full);
        }
    } else {
        cJSON_AddStringToObject(usr, "content", prompt);
    }
    cJSON_AddItemToArray(messages, usr);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "Usage: command | %s \"prompt\"\n", argv[0]); return 1; }

    // No need to malloc/free these. getenv returns a pointer to environment block.
    const char *url = getenv("INFER_API_URL");
    const char *key = getenv("INFER_API_KEY");
    const char *model = getenv("INFER_MODEL");

    if (!url) { fprintf(stderr, "Error: INFER_API_URL not set.\n"); return 1; }

    char *pipe_in = read_stdin();
    
    // Quick argument join (O(N) single pass)
    // 1. Calculate length
    size_t p_len = 0;
    for(int i=1; i<argc; i++) p_len += strlen(argv[i]) + 1;
    
    // 2. Build string
    char *prompt = malloc(p_len + 1);
    if (!prompt) return 1;
    char *p = prompt;
    for(int i=1; i<argc; i++) {
        size_t len = strlen(argv[i]);
        memcpy(p, argv[i], len);
        p += len;
        if (i < argc-1) *p++ = ' ';
    }
    *p = 0;

    // Execute
    struct response chunk = {0};
    char *payload = build_payload(model, prompt, pipe_in);
    int exit_code = 1;

    if (payload) {
        CURL *c = curl_easy_init();
        if (c) {
            struct curl_slist *h = NULL;
            h = curl_slist_append(h, "Content-Type: application/json");
            if (key) {
                char auth[256]; snprintf(auth, sizeof(auth), "Authorization: Bearer %s", key);
                h = curl_slist_append(h, auth);
            }

            curl_easy_setopt(c, CURLOPT_URL, url);
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, payload);
            curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
            curl_easy_setopt(c, CURLOPT_WRITEDATA, (void *)&chunk);
            
            if (curl_easy_perform(c) == CURLE_OK && chunk.data) {
                cJSON *json = cJSON_Parse(chunk.data);
                // Simplify extraction: Just grab choices[0].message.content
                cJSON *content = cJSON_GetObjectItem(
                    cJSON_GetObjectItem(
                        cJSON_GetArrayItem(cJSON_GetObjectItem(json, "choices"), 0),
                        "message"
                    ), 
                    "content"
                );
                
                if (cJSON_IsString(content)) {
                    printf("%s\n", content->valuestring);
                    exit_code = 0;
                } else {
                    fprintf(stderr, "Invalid API response\n");
                }
                cJSON_Delete(json);
            }
            curl_slist_free_all(h);
            curl_easy_cleanup(c);
        }
        free(payload);
    }

    free(prompt);
    free(pipe_in);
    free(chunk.data);
    return exit_code;
}
