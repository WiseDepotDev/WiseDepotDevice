/**
 * 日志上传服务模块实现
 *
 * @author xingchentye
 * @version 0.1.0
 * @since 2026-03-13
 */

#include "application/log_service.h"
#include "common/config.h"
#include "common/logger.h"
#include "infrastructure/http_client.h"
#include "common/xmalloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>

#define UPLOAD_QUEUE_PREFIX "upload_queue_"
#define LOG_MAX_SIZE (1024 * 1024) // 1MB

int log_service_init(void) {
    LOG_INFO("Log service initialized");
    return 0;
}

static void process_upload_queue(const Config *cfg) {
    // Scan directory for files starting with UPLOAD_QUEUE_PREFIX
    // For simplicity, we assume logs are in current directory (where we run)
    // Or check cfg->log_path directory.
    
    DIR *d;
    struct dirent *dir;
    d = opendir(".");
    if (!d) return;

    while ((dir = readdir(d)) != NULL) {
        if (strncmp(dir->d_name, UPLOAD_QUEUE_PREFIX, strlen(UPLOAD_QUEUE_PREFIX)) == 0) {
            LOG_INFO("Found pending log file: %s", dir->d_name);
            
            // Read file content
            FILE *fp = fopen(dir->d_name, "rb");
            if (!fp) continue;
            
            fseek(fp, 0, SEEK_END);
            long fsize = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            
            if (fsize > 0) {
                char *buffer = xmalloc(fsize + 1);
                fread(buffer, 1, fsize, fp);
                buffer[fsize] = '\0';
                
                // Construct JSON payload
                // { "deviceId": "...", "logContent": "..." }
                // Note: Escaping log content for JSON is needed if we put it in JSON string.
                // For simplicity, let's assume we use a raw text upload or handle escaping.
                // A better way is to use cJSON to create the object which handles escaping.
                
                // But wait, if file is large, loading into memory is bad.
                // Ideally we use streaming upload.
                // http_client doesn't support streaming yet.
                // We'll limit log size to 1MB.
                
                char url[1024];
                snprintf(url, sizeof(url), "%s%s/logs/upload?deviceId=%s", 
                         cfg->server_url, cfg->api_base_url, cfg->device_id);
                
                // We can use multipart/form-data but our http_client is simple.
                // Let's send raw text with Content-Type: text/plain
                const char *headers[] = { "Content-Type: text/plain" };
                
                HttpResponse *res = http_post_with_retry(url, buffer, headers, 1, 10000, 3);
                
                if (res && res->status_code == 200) {
                    LOG_INFO("Log uploaded successfully: %s", dir->d_name);
                    fclose(fp);
                    unlink(dir->d_name); // Delete file
                } else {
                    LOG_WARN("Failed to upload log: %s (Status: %d)", dir->d_name, res ? res->status_code : -1);
                    fclose(fp);
                }
                
                if (res) http_response_free(res);
                xfree(buffer);
            } else {
                fclose(fp);
                unlink(dir->d_name); // Delete empty file
            }
        }
    }
    closedir(d);
}

void log_upload_task(void *ctx) {
    (void)ctx;
    const Config *cfg = config_get();
    
    // Check if current log file needs rotation
    struct stat st;
    if (stat(cfg->log_path, &st) == 0) {
        if (st.st_size > LOG_MAX_SIZE) {
            // Rotate
            char new_name[256];
            snprintf(new_name, sizeof(new_name), "%s%ld.log", UPLOAD_QUEUE_PREFIX, (long)time(NULL));
            
            if (rename(cfg->log_path, new_name) == 0) {
                LOG_INFO("Rotated log file to %s", new_name);
                logger_reopen(NULL); // Reopen original path
            } else {
                LOG_ERROR("Failed to rotate log file");
            }
        }
    }
    
    // Process queue
    process_upload_queue(cfg);
}

void log_service_stop(void) {
    // 
}
