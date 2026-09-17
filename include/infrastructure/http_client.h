/**
 * HTTP 客户端模块头文件
 *
 * @author xingchentye
 * @version 0.1.0
 * @since 2026-03-06
 */

#ifndef WISE_DEPOT_HTTP_CLIENT_H
#define WISE_DEPOT_HTTP_CLIENT_H

#include <stddef.h>

/**
 * HTTP 响应结构体
 */
typedef struct {
    int status_code;    /**< HTTP 状态码 */
    char *body;         /**< 响应体 (需调用者释放) */
    size_t body_len;    /**< 响应体长度 */
} HttpResponse;

/**
 * 发送 HTTP GET 请求 (支持自定义头)
 *
 * @param url 目标 URL
 * @param headers 自定义头数组 (如 "Header: Value")
 * @param header_count 头数量
 * @return 响应结构体指针 (失败返回 NULL)
 */
HttpResponse *http_get(const char *url, const char **headers, int header_count);

/**
 * 发送 HTTP POST 请求 (支持自定义头)
 *
 * @param url 目标 URL
 * @param json_body JSON 格式的请求体
 * @param headers 自定义头数组 (如 "Header: Value")
 * @param header_count 头数量
 * @return 响应结构体指针 (失败返回 NULL)
 */
HttpResponse *http_post(const char *url, const char *json_body, const char **headers, int header_count);

/**
 * 发送 HTTP PUT 请求 (支持自定义头)
 *
 * @param url 目标 URL
 * @param json_body JSON 格式的请求体
 * @param headers 自定义头数组 (如 "Header: Value")
 * @param header_count 头数量
 * @return 响应结构体指针 (失败返回 NULL)
 */
HttpResponse *http_put(const char *url, const char *json_body, const char **headers, int header_count);

/**
 * 释放 HTTP 响应资源
 *
 * @param res 响应结构体指针
 */
void http_response_free(HttpResponse *res);

/**
 * 发送 HTTP POST 请求 (带重试与自定义头)
 *
 * @param url 目标 URL
 * @param json_body JSON 请求体
 * @param headers 自定义头数组
 * @param header_count 头数量
 * @param timeout_ms 超时时间 (毫秒)
 * @param max_retries 最大重试次数
 * @return 响应结构体指针 (失败返回 NULL)
 */
HttpResponse *http_post_with_retry(const char *url, const char *json_body, const char **headers, int header_count, int timeout_ms, int max_retries);

#endif // WISE_DEPOT_HTTP_CLIENT_H
