/**
 * @file wd_error.h
 * @brief 设备端统一错误类型出口（STD-CODE-06）。
 *
 * 设计要点：
 * 1. **唯一错误类型**：设备端所有错误返回值一律使用 `wd_error_t`，禁止裸 `-1`/`-2/-3/-4`
 *    等魔数表达错误语义；
 * 2. **全部为负**：`WD_OK == 0`，其余全部为负数，因此 `!= 0`、`< 0`、`> 0` 这类既有调用点
 *    写法在迁移期完全兼容（P4-11 迁移 149 处 `return -1` 时零行为变化）；
 * 3. **与错误码字符串表分工**：`common/error_code.h`（自动生成，来自服务端
 *    `后端异常码对照表.csv`）承载**对外契约**的业务码字符串（`RES-0000` 等）；
 *    本文件承载**进程内**的函数返回码。两者不同层，不得互相替代。
 *
 * @note 新增值必须在此登记并写进 `wd_error_str()`，否则设备端日志只能打数字。
 */

#ifndef WISE_DEPOT_COMMON_WD_ERROR_H
#define WISE_DEPOT_COMMON_WD_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/** 设备端统一错误码（全部为负，`WD_OK` 为 0） */
typedef enum {
    WD_OK              =    0, /**< 成功 */
    WD_ERR_PARAM       =   -1, /**< 参数非法：空指针、越界、取值不在枚举内 */
    WD_ERR_TIMEOUT     =   -2, /**< 超时（串口/网络等待） */
    WD_ERR_PROTOCOL    =   -3, /**< 报文不合法：长度、状态字、帧结构不符 */
    WD_ERR_CRC         =   -4, /**< 校验失败（CRC 不匹配） */
    WD_ERR_IO          =   -5, /**< 设备/文件读写失败（I2C、GPIO、open/write/stat） */
    WD_ERR_NOMEM       =   -6, /**< 内存分配失败 */
    WD_ERR_STATE       =   -7, /**< 前置状态不满足：未初始化、重复启动、未在运行 */
    WD_ERR_NOT_FOUND   =   -8, /**< 目标不存在 */
    WD_ERR_BUSY        =   -9, /**< 资源忙 */
    WD_ERR_FULL        =  -10, /**< 容量已满 */
    WD_ERR_UNSUPPORTED =  -11, /**< 不支持的操作/取值 */
    WD_ERR_CRYPTO      =  -12, /**< 加解密与签名失败 */
    WD_ERR_CONNECT     =  -13, /**< 连接失败（服务发现 / MQTT / HTTP） */
    WD_ERR_CANCELED    =  -14, /**< 被取消（任务取消语义） */
    WD_ERR_ACTION      =  -15, /**< 动作执行失败（巡检动作回调/执行体） */
    WD_ERR_SERVER      =  -16, /**< 服务端返回非成功响应（HTTP 非 2xx / 业务码失败） */
    WD_ERR_AUTH        =  -17, /**< 认证或授权失败（Token 无效/过期/权限不足） */
    WD_ERR_GENERAL     = -100  /**< 未归类的通用失败；**禁止新增**，仅用于迁移与兜底 */
} wd_error_t;

/**
 * @brief 取错误码的稳定字符串名（用于日志，不用于对外契约）。
 * @param err 错误码
 * @return 形如 "WD_ERR_TIMEOUT" 的静态字符串；未知值返回 "WD_ERR_UNKNOWN"
 */
const char *wd_error_str(wd_error_t err);

#ifdef __cplusplus
}
#endif

#endif /* WISE_DEPOT_COMMON_WD_ERROR_H */
