/**
 * HTTP 客户端模块实现 (基于 libcurl)
 *
 * @author xingchentye
 * @version 0.2.0
 * @since 2026-03-13
 */

#include "infrastructure/http_client.h"
#include "common/xmalloc.h"
#include "common/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <strings.h>

typedef struct {
    char *data;
    size_t size;
} MemoryStruct;

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryStruct *mem = (MemoryStruct *)userp;

    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        LOG_ERROR("Not enough memory (realloc returned NULL)");
        return 0;
    }

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

static http_response_t *perform_request(const char *method, const char *url, const char *body, const char **headers, int header_count, int timeout_ms) {
    CURL *curl;
    CURLcode res;
    http_response_t *response = NULL;
    struct curl_slist *chunk = NULL;

    curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("Failed to init curl");
        return NULL;
    }

    MemoryStruct chunk_data;
    chunk_data.data = xmalloc_try(1);
    if (!chunk_data.data) {
        LOG_ERROR("分配 HTTP 响应缓冲失败（内存不足）");
        curl_easy_cleanup(curl);
        return NULL;
    }
    chunk_data.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk_data);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "wise-depot-device/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms > 0 ? timeout_ms : 5000L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects
    
    /* 生产必须保持校验开启（系统 CA 由部署方安装）；自签名联调才临时放开。 */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L); 
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        }
    } else if (strcmp(method, "PUT") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        }
    } else {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    // Headers
    // Always add Content-Type: application/json if body is present, unless overridden
    int has_content_type = 0;
    if (headers && header_count > 0) {
        for (int i = 0; i < header_count; i++) {
            chunk = curl_slist_append(chunk, headers[i]);
            if (strncasecmp(headers[i], "Content-Type", 12) == 0) {
                has_content_type = 1;
            }
        }
    }
    
    if (body && !has_content_type) {
        chunk = curl_slist_append(chunk, "Content-Type: application/json");
    }
    
    if (chunk) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    }

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        LOG_ERROR("curl_easy_perform() failed: %s", curl_easy_strerror(res));
        xfree(chunk_data.data);
    } else {
        response = (http_response_t *)xmalloc_try(sizeof(http_response_t));
        if (!response) {
            /* P4-06：内存不足返回 NULL（调用方按"无响应"处理），不再退出进程 */
            LOG_ERROR("分配 HTTP 响应结构失败（内存不足）");
            xfree(chunk_data.data);
            curl_easy_cleanup(curl);
            return NULL;
        }
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        response->status_code = (int)response_code;
        response->body = chunk_data.data; // Transfer ownership
        response->body_len = chunk_data.size;
    }

    curl_easy_cleanup(curl);
    if (chunk) curl_slist_free_all(chunk);
    
    return response;
}

http_response_t *http_get(const char *url, const char **headers, int header_count) {
    return perform_request("GET", url, NULL, headers, header_count, 5000);
}

http_response_t *http_post(const char *url, const char *json_body, const char **headers, int header_count) {
    return perform_request("POST", url, json_body, headers, header_count, 5000);
}

http_response_t *http_put(const char *url, const char *json_body, const char **headers, int header_count) {
    return perform_request("PUT", url, json_body, headers, header_count, 5000);
}

http_response_t *http_post_with_retry(const char *url, const char *json_body, const char **headers, int header_count, int timeout_ms, int max_retries) {
    http_response_t *res = NULL;
    int attempt = 0;
    
    while (attempt <= max_retries) {
        res = perform_request("POST", url, json_body, headers, header_count, timeout_ms);
        if (res) {
            // Check for 5xx errors or network errors? 
            // perform_request returns NULL on network error (curl fail).
            // If res is not NULL, network is OK.
            // If status code is 5xx, maybe retry?
            if (res->status_code >= 500) {
                LOG_WARN("Server error %d, retrying (%d/%d)...", res->status_code, attempt + 1, max_retries);
                http_response_free(res);
                res = NULL;
            } else {
                return res;
            }
        } else {
            LOG_WARN("Network error, retrying (%d/%d)...", attempt + 1, max_retries);
        }
        
        attempt++;
        if (attempt <= max_retries) {
            // Exponential backoff
            usleep((1 << attempt) * 100000); // 100ms * 2^attempt
        }
    }
    
    return NULL;
}

void http_response_free(http_response_t *res) {
    if (res) {
        if (res->body) xfree(res->body);
        xfree(res);
    }
}
