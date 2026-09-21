/**
 * @file patrol_action.c
 * @brief 单个巡检动作的执行与扫描累积（P4-10 批次2 从 patrol_service.c 拆出，仅搬不改）。
 */

#include "application/patrol_internal.h"
#include "application/rfid_service.h"
#include "application/motor_service.h"
#include "common/config.h"
#include "common/crypto.h"
#include "common/envelope.h"
#include "infrastructure/http_client.h"
#include "common/logger.h"
#include "common/xmalloc.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
/** 判断两个标签的 EPC 是否相同 */
static bool tag_epc_equal(const rfid_tag_t *a, const rfid_tag_t *b) {
    if (a->epc_len != b->epc_len) return false;
    return memcmp(a->epc, b->epc, a->epc_len) == 0;
}

/** 将本批扫描到的标签合并到巡检上下文中（按 EPC 去重） */
static void merge_scan_into_context(patrol_scan_context_t *ctx, const rfid_tag_t *batch, int batch_count) {
    for (int i = 0; i < batch_count && ctx->count < ctx->capacity; i++) {
        bool found = false;
        for (size_t k = 0; k < ctx->count; k++) {
            if (tag_epc_equal(&ctx->tags[k], &batch[i])) {
                found = true;
                break;
            }
        }
        if (!found) {
            memcpy(&ctx->tags[ctx->count], &batch[i], sizeof(rfid_tag_t));
            ctx->count++;
        }
    }
}

int patrol_action_callback(const patrol_task_t *task, uint8_t action_index, void *context) {
    patrol_scan_context_t *scan_ctx = (patrol_scan_context_t *)context;
    if (action_index >= task->action_count) return -1;
    const patrol_action_t *action = &task->actions[action_index];
    
    // 移动动作：移动过程中仅做 RFID 扫描并累积到 context，不拉取库存、不上传
    if (action->type == PATROL_ACTION_MOVE_FORWARD || 
        action->type == PATROL_ACTION_MOVE_BACKWARD ||
        action->type == PATROL_ACTION_MOVE_LEFT ||
        action->type == PATROL_ACTION_MOVE_RIGHT ||
        action->type == PATROL_ACTION_TURN_LEFT ||
        action->type == PATROL_ACTION_TURN_RIGHT) {
        
        LOG_INFO("Executing move action with RFID scanning (accumulate only): %s", 
                 patrol_action_type_to_string(action->type));
        
        move_direction_t dir;
        switch(action->type) {
            case PATROL_ACTION_MOVE_FORWARD: dir = MOVE_FORWARD; break;
            case PATROL_ACTION_MOVE_BACKWARD: dir = MOVE_BACKWARD; break;
            case PATROL_ACTION_MOVE_LEFT: dir = MOVE_LEFT; break;
            case PATROL_ACTION_MOVE_RIGHT: dir = MOVE_RIGHT; break;
            case PATROL_ACTION_TURN_LEFT: dir = MOVE_TURN_LEFT; break;
            case PATROL_ACTION_TURN_RIGHT: dir = MOVE_TURN_RIGHT; break;
            default: dir = MOVE_FORWARD; break; 
        }
        
        if (motor_move_async(dir, action->speed) < 0) {
            LOG_ERROR("Failed to start motor movement");
            return -1;
        }
        
        struct timespec start_time, current_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        long elapsed_ms = 0;
        rfid_tag_t *batch = (rfid_tag_t *)xcalloc_try(PATROL_SCAN_BATCH_SIZE, sizeof(rfid_tag_t));
        if (!batch) {
            LOG_ERROR("分配扫码缓冲区失败（内存不足），巡检动作中止");
            return -1;
        }
        
        while (elapsed_ms < action->duration_ms) {
            /* P4-07：动作执行期间也轮询本任务的取消标志，保证取消及时生效 */
            if (task->cancel_requested) {
                motor_stop_all();
                return 1; /* 由上层循环标记 CANCELLED（不当作动作失败） */
            }
            int n = rfid_service_scan_only(batch, PATROL_SCAN_BATCH_SIZE);
            if (n > 0 && scan_ctx) {
                merge_scan_into_context(scan_ctx, batch, n);
            }
            
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            elapsed_ms = (current_time.tv_sec - start_time.tv_sec) * 1000 + 
                         (current_time.tv_nsec - start_time.tv_nsec) / 1000000;
            
            if (elapsed_ms % 1000 < 100) {
                int progress = (int)((elapsed_ms * 100) / action->duration_ms);
                if (progress > 100) progress = 100;
                char url[1024];
                snprintf(url, sizeof(url), "%s/api/inspection/task/%s/progress?progress=%d", 
                         config_get()->server_url, task->id, progress);
                char timestamp[20];
                snprintf(timestamp, sizeof(timestamp), "%ld", (long)time(NULL));
                char nonce[32];
                snprintf(nonce, sizeof(nonce), "%s%d", timestamp, rand());
                char uri_path[256];
                snprintf(uri_path, sizeof(uri_path), "/api/inspection/task/%s/progress", task->id);
                char query_string[256];
                snprintf(query_string, sizeof(query_string), "nonce=%s&progress=%d&timestamp=%s", nonce, progress, timestamp);
                char string_to_sign[2048];
                snprintf(string_to_sign, sizeof(string_to_sign), "PUT\n%s\n%s", uri_path, query_string);
                unsigned char hmac_result[32];
                const char *signing_secret = config_signature_secret();
                if (signing_secret == NULL) {
                    LOG_ERROR("Signature secret not configured (WISE_API_SIGNATURE_SECRET / signature_secret); progress report skipped");
                    continue; /* 进度上报不阻断巡检动作本身 */
                }
                hmac_sha256(signing_secret, strlen(signing_secret), string_to_sign, strlen(string_to_sign), hmac_result);
                size_t sig_len = 0;
                char *signature = base64_encode(hmac_result, 32, &sig_len);
                char header_sign[256], header_time[64], header_nonce[64];
                snprintf(header_sign, sizeof(header_sign), "X-Signature: %s", signature);
                snprintf(header_time, sizeof(header_time), "X-Timestamp: %s", timestamp);
                snprintf(header_nonce, sizeof(header_nonce), "X-Nonce: %s", nonce);
                const char *headers[] = { header_sign, header_time, header_nonce };
                http_response_t *res = http_put(url, NULL, headers, 3);
                if (res) {
                    if (res->status_code >= 200 && res->status_code < 300) {
                        LOG_DEBUG("Progress updated: %d%%", progress);
                    } else {
                        LOG_ERROR("Progress update failed (Status: %d)", res->status_code);
                    }
                    http_response_free(res);
                }
                xfree(signature);
            }
            
            usleep(100000); // 100ms 间隔再扫
        }
        
        xfree(batch);
        motor_service_stop_all();
        return 1;
    }
    
    if (action->type == PATROL_ACTION_RFID_SCAN) {
        LOG_INFO("Executing RFID Scan Action (accumulate only)");
        rfid_tag_t *batch = (rfid_tag_t *)xcalloc_try(PATROL_SCAN_BATCH_SIZE, sizeof(rfid_tag_t));
        if (!batch) {
            LOG_ERROR("分配扫码缓冲区失败（内存不足），RFID 盘点动作中止");
            return -1;
        }
        int n = rfid_service_scan_only(batch, PATROL_SCAN_BATCH_SIZE);
        if (n > 0 && scan_ctx) {
            merge_scan_into_context(scan_ctx, batch, n);
        }
        xfree(batch);
        return 1;
    }
    return 0;
}