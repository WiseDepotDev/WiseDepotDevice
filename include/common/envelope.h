/**
 * @file envelope.h
 * @brief 设备端统一信封（Envelope）解析模块。
 *
 * 统一报文结构（三端一致，见 docs/standards/慧仓智控统一开发标准-v1.0.md STD-CONTRACT-01）：
 * @code
 * {
 *   "header":  { "request_id": "...", "packet_type": "...", "timestamp": 0 },
 *   "payload": { "code": "RES-0000", "message": "处理成功", "data": {}, "errorCode": null }
 * }
 * @endcode
 *
 * 设计要点：
 * - 本模块是设备端**唯一**的信封解析入口；业务代码不得再自行读取 `code` / `00000` 等兼容分支
 *   （STD-ERR-02：单一错误出口；STD-CONTRACT-03：契约单一来源）。
 * - 业务码与错误码比较统一走 common/error_code.h 中由生成器产出的宏，禁止再写字符串字面量。
 * - 本模块持有 cJSON 解析树的**所有权**，使用方必须在结束前调用 envelope_free()。
 */

#ifndef WISE_DEPOT_COMMON_ENVELOPE_H
#define WISE_DEPOT_COMMON_ENVELOPE_H

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 请求信封 payload.code 固定值。
 *
 * 服务端请求侧不校验业务码，但 `docs/standards/schemas/envelope.schema.json` 要求
 * payload 必须带 code/message，因此请求一律写成功码（与响应侧语义一致）。
 */
#define WD_ENVELOPE_REQUEST_CODE "RES-0000"

/** request_id 缓冲建议容量：schema 要求 8..128，时间戳+随机后缀实测 20 字符左右 */
#define WD_ENVELOPE_REQUEST_ID_CAP 96

/** 信封解析结果码 */
typedef enum {
    WD_ENVELOPE_OK = 0,            /**< 解析成功且载荷结构合法 */
    WD_ENVELOPE_ERR_NULL = -1,     /**< 入参为 NULL */
    WD_ENVELOPE_ERR_PARSE = -2,    /**< JSON 解析失败 */
    WD_ENVELOPE_ERR_SHAPE = -3,    /**< 缺少 header 或 payload，或类型不符 */
    WD_ENVELOPE_ERR_CODE = -4,     /**< payload 中缺少 business code */
    WD_ENVELOPE_ERR_NOT_SUCCESS = -5 /**< 业务码表示失败 */
} wd_envelope_result_t;

/** 信封头信息（定长拷贝，避免使用方持有 cJSON 内部指针） */
typedef struct {
    char request_id[128];  /**< 链路标识，缺失时为空串 */
    char packet_type[64];  /**< 报文类型，缺失时为空串 */
    int64_t timestamp;     /**< 毫秒时间戳（int64_t，避免 32 位 long 溢出），缺失时为 0 */
} wd_envelope_header_t;

/** 信封解析结果 */
typedef struct {
    cJSON *root;              /**< 解析树根，由本结构持有所有权 */
    cJSON *payload;           /**< payload 节点（指向 root 内部） */
    wd_envelope_header_t header; /**< 头信息副本 */
} wd_envelope_t;

/**
 * @brief 解析信封字符串。
 *
 * 只接受标准信封结构（header + payload）；不再兼容「根节点直接带 code」等历史格式。
 *
 * @param json_str JSON 文本，不得为 NULL
 * @param out      输出结构，成功时持有 root 所有权；失败时不持有
 * @return wd_envelope_result_t 结果码
 */
int envelope_parse(const char *json_str, wd_envelope_t *out);

/**
 * @brief 把「扁平业务字段」包装成标准请求信封（STD-CONTRACT-02 请求侧）。
 *
 * 请求侧信封化是唯一的请求体构造出口：业务代码只准备扁平 JSON，
 * 由本函数补 `header`（request_id / packet_type / timestamp）与
 * `payload`（code=RES-0000 / message / data=原扁平对象）。
 * 服务端 `GlobalRequestAdvice` 解包后只把 `payload.data` 交给控制器，
 * 因此包装后控制器收到的字段与包装前逐字一致。
 *
 * **不参与签名**：设备端签名串固定为 `METHOD\nURI\nquery`
 * （见 RequestSignatureService），请求体不在其中，故信封化不影响签名校验。
 *
 * @param packet_type  报文类型，必须是大写字母/数字/下划线且取自服务端 PacketType
 *                     （如 DEVICE_CREATE / DEVICE_HEARTBEAT / RFID_DATA_UPLOAD）；
 *                     无对应类型时用 UNKNOWN。空串或 NULL 视为调用错误，直接返回 NULL。
 * @param flat_json    扁平业务字段 JSON 对象文本；NULL 或空串表示无字段（payload.data 为 {}）。
 *                     解析失败或根节点不是对象时返回 NULL（调用方必须自己决定降级策略）。
 * @param request_id_out 非 NULL 时写入本次生成的 request_id（用于 HTTP 头 REQUEST-ID 透传）
 * @param request_id_cap  该缓冲容量，建议 WD_ENVELOPE_REQUEST_ID_CAP
 * @return 信封 JSON 文本；**由 cJSON 用 malloc 分配，调用方必须用 free() 释放**
 *         （与 config.c 中 cJSON_Print* 的约定一致，不可用 xfree）；
 *         失败返回 NULL
 */
char *envelope_wrap_request(const char *packet_type, const char *flat_json, char *request_id_out,
                            size_t request_id_cap);

/**
 * @brief 释放信封持有的解析树。
 *
 * @param env 信封结构，可为 NULL（幂等）
 */
void envelope_free(wd_envelope_t *env);

/**
 * @brief 取 payload 中的业务码。
 *
 * @param env 信封结构
 * @return 业务码字符串；不存在时返回 NULL
 */
const char *envelope_code(const wd_envelope_t *env);

/**
 * @brief 取 payload 中的错误码（失败时才有）。
 *
 * @param env 信封结构
 * @return 错误码字符串；不存在时返回 NULL
 */
const char *envelope_error_code(const wd_envelope_t *env);

/**
 * @brief 取 payload 中的提示信息。
 *
 * @param env 信封结构
 * @return 消息字符串；不存在时返回 NULL
 */
const char *envelope_message(const wd_envelope_t *env);

/**
 * @brief 判断业务码是否为成功码。
 *
 * @param env 信封结构
 * @return 1 表示成功，0 表示失败
 */
int envelope_is_success(const wd_envelope_t *env);

/**
 * @brief 取 payload.data 节点。
 *
 * @param env 信封结构
 * @return data 节点；不存在或为 null 时返回 NULL（返回值为借用，勿释放）
 */
cJSON *envelope_data(const wd_envelope_t *env);

/**
 * @brief 取列表载荷：data 为数组时返回 data；data 为对象且含 rows 数组时返回 rows。
 *
 * @param env 信封结构
 * @return 数组节点；无列表时返回 NULL（返回值为借用，勿释放）
 */
cJSON *envelope_data_rows(const wd_envelope_t *env);

/**
 * @brief 从对象中安全读取字符串字段。
 *
 * 替代各业务文件中重复实现的 extract_json_string()。
 *
 * @param obj 对象节点，可为 NULL
 * @param key 字段名
 * @return 字段字符串；不存在或类型不符时返回 NULL
 */
const char *envelope_str(const cJSON *obj, const char *key);

/**
 * @brief 从对象中安全读取整数字段。
 *
 * @param obj 对象节点，可为 NULL
 * @param key 字段名
 * @param default_value 字段缺失时的返回值
 * @return 整数值
 */
int64_t envelope_int(const cJSON *obj, const char *key, int64_t default_value);

#ifdef __cplusplus
}
#endif

#endif /* WISE_DEPOT_COMMON_ENVELOPE_H */
