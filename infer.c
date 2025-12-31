#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <curl/curl.h>
#include "jsmn.h"

#define MAX_LINE 1024
#define MAX_VAL  512

// Config globals
static char api_url[MAX_VAL];
static char api_key[MAX_VAL];
static char model[MAX_VAL];

static const char *SYSTEM_PROMPT = 
    "You are a CLI tool. Output plain text only. No yapping. Keep the output concise. "
    "DO NOT USE MARKDOWNS. NO asterisks. NO backticks. NO formatting. NO \\n literals "
    "Just plain readable sentences.";


/* ---------------- HELPERS ---------------- */

// Minimal JSON string escaper (handles ", \, and newline)
// Returns a new allocated string you must free.
static char* json_escape(const char *src) {
    if (!src) return calloc(1, 1);
    char *dest = malloc(strlen(src) * 2 + 1); // Worst case size
    char *p = dest;
    while (*src) {
        if (*src == '"' || *src == '\\') { *p++ = '\\'; *p++ = *src; }
        else if (*src == '\n') { *p++ = '\\'; *p++ = 'n'; }
        else if (*src == '\r') { /* skip */ }
        else *p++ = *src;
        src++;
    }
    *p = 0;
    return dest;
}

// Reads stdin into a dynamically allocated string
static char* read_stdin() {
    if (isatty(fileno(stdin))) return NULL; // No pipe detected

    size_t size = 4096, len = 0;
    char *buf = malloc(size);
    int c;
    while ((c = getchar()) != EOF) {
        buf[len++] = c;
        if (len >= size - 1) {
            size *= 2;
            buf = realloc(buf, size);
        }
    }
    buf[len] = 0;
    return buf;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static void emit_utf8(uint32_t cp) {
    if (cp <= 0x7F) {
        fputc((int)cp, stdout);
    } else if (cp <= 0x7FF) {
        fputc(0xC0 | ((cp >> 6) & 0x1F), stdout);
        fputc(0x80 | (cp & 0x3F), stdout);
    } else if (cp <= 0xFFFF) {
        fputc(0xE0 | ((cp >> 12) & 0x0F), stdout);
        fputc(0x80 | ((cp >> 6) & 0x3F), stdout);
        fputc(0x80 | (cp & 0x3F), stdout);
    } else if (cp <= 0x10FFFF) {
        fputc(0xF0 | ((cp >> 18) & 0x07), stdout);
        fputc(0x80 | ((cp >> 12) & 0x3F), stdout);
        fputc(0x80 | ((cp >> 6) & 0x3F), stdout);
        fputc(0x80 | (cp & 0x3F), stdout);
    } else {
        fputc('?', stdout);
    }
}

static void print_json_string_unescaped(const char *s, int len) {
    int i = 0;
    while (i < len) {
        char c = s[i++];
        if (c != '\\') {
            fputc(c, stdout);
            continue;
        }
        if (i >= len) { fputc('\\', stdout); break; }
        char esc = s[i++];
        switch (esc) {
            case 'n': fputc('\n', stdout); break;
            case 'r': fputc('\r', stdout); break;
            case 't': fputc('\t', stdout); break;
            case 'b': fputc('\b', stdout); break;
            case 'f': fputc('\f', stdout); break;
            case '"': fputc('"', stdout); break;
            case '\\': fputc('\\', stdout); break;
            case '/': fputc('/', stdout); break;
            case 'u': {
                if (i + 4 > len) { fputc('?', stdout); break; }
                uint32_t cp = 0;
                for (int k = 0; k < 4; k++) {
                    int hv = hexval(s[i + k]);
                    if (hv < 0) { cp = 0xFFFD; break; }
                    cp = (cp << 4) | (uint32_t)hv;
                }
                i += 4;

                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (i + 6 <= len && s[i] == '\\' && s[i + 1] == 'u') {
                        uint32_t low = 0;
                        int ok = 1;
                        for (int k = 0; k < 4; k++) {
                            int hv = hexval(s[i + 2 + k]);
                            if (hv < 0) { ok = 0; break; }
                            low = (low << 4) | (uint32_t)hv;
                        }
                        if (ok && low >= 0xDC00 && low <= 0xDFFF) {
                            i += 6;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                    }
                }
                emit_utf8(cp);
                break;
            }
            default:
                fputc(esc, stdout);
                break;
        }
    }
}

/* ---------------- YAML PARSER (REMOVED) ---------------- */

/* ---------------- HTTP RESPONSE HANDLING ---------------- */

struct response {
    char *data;
    size_t size;
};

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t realsize = size * nmemb;
    struct response *mem = (struct response *)userdata;
    
    char *ptr_realloc = realloc(mem->data, mem->size + realsize + 1);
    if(!ptr_realloc) return 0; // Out of memory

    mem->data = ptr_realloc;
    memcpy(&(mem->data[mem->size]), ptr, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    return realsize;
}

/* ---------------- MAIN ---------------- */

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s \"prompt\"\n", argv[0]); return 1; }

    // Load from Environment Variables
    char *env_url = getenv("INFER_API_URL");
    char *env_key = getenv("INFER_API_KEY");
    char *env_model = getenv("INFER_MODEL");

    if (!env_url || !*env_url) {
        fprintf(stderr, "Error: INFER_API_URL environment variable not set.\n");
        fprintf(stderr, "Please set INFER_API_URL, INFER_API_KEY, and INFER_MODEL.\n");
        return 1;
    }

    snprintf(api_url, sizeof(api_url), "%s", env_url);
    if (env_key) snprintf(api_key, sizeof(api_key), "%s", env_key);
    if (env_model) snprintf(model, sizeof(model), "%s", env_model);

    // 1. Prepare Inputs
    char *pipe_in = read_stdin();
    char *safe_prompt = json_escape(argv[1]);
    char *safe_pipe = json_escape(pipe_in);
    
    // 2. Build Payload
    char *payload_fmt = 
        "{"
        "\"model\":\"%s\","
        "\"stream\":false,"
        "\"messages\":["
          "{\"role\":\"system\",\"content\":\"%s\"},"
          "{\"role\":\"user\",\"content\":\"%s\\n\\nContext:\\n%s\"}"
        "]"
        "}";

    // Allocate exact size needed
    size_t plen = strlen(payload_fmt) + strlen(model) + strlen(SYSTEM_PROMPT) + 
                  strlen(safe_prompt) + strlen(safe_pipe?safe_pipe:"") + 100;
    char *payload = malloc(plen);
    snprintf(payload, plen, payload_fmt, model, SYSTEM_PROMPT, safe_prompt, safe_pipe?safe_pipe:"");

    // 3. Setup Curl
    struct response chunk = {0};
    CURL *c = curl_easy_init();
    struct curl_slist *h = NULL;
    char auth[1024]; snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);
    
    h = curl_slist_append(h, "Content-Type: application/json");
    h = curl_slist_append(h, auth);

    curl_easy_setopt(c, CURLOPT_URL, api_url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, (void *)&chunk);

    // 4. Perform
    CURLcode res = curl_easy_perform(c);

    // 5. Parse JSON Response
    if (res == CURLE_OK && chunk.data) {
        jsmn_parser p;
        jsmntok_t tok[1024]; // Assuming response isn't massively complex
        jsmn_init(&p);
        int r = jsmn_parse(&p, chunk.data, chunk.size, tok, 1024);

        // Simple scan for "content" key
        for (int i = 1; i < r; i++) {
            if (tok[i].type == JSMN_STRING && 
                strncmp(chunk.data + tok[i].start, "content", 7) == 0) {
                
                // Print the value immediately following "content"
                jsmntok_t val = tok[i+1];
                print_json_string_unescaped(chunk.data + val.start, val.end - val.start);
                fputc('\n', stdout);
                break; // Found it, exit
            }
        }
    } else {
        fprintf(stderr, "Request failed: %s\n", curl_easy_strerror(res));
    }

    // Cleanup
    free(pipe_in); free(safe_prompt); free(safe_pipe); free(payload); free(chunk.data);
    curl_easy_cleanup(c);
    return 0;
}
