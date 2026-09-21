/**
 * @file tag.h
 * @brief RFID 标签**领域值对象**（P4-09：由 infrastructure 下沉到 domain）。
 *
 * 为什么放这里：`inventory_manager`（domain）需要标签类型，但此前它直接 include
 * `infrastructure/rfid_driver.h`，导致 **domain 依赖 infrastructure**（分层倒挂）。
 * 现在由 domain 拥有该值对象，infrastructure 的驱动头再反向引用它——
 * 依赖方向变为 infrastructure → domain，符合分层。
 *
 * 注意：这里只放"纯数据"，不含任何驱动/串口/协议细节。
 */

#ifndef WISE_DEPOT_DOMAIN_TAG_H
#define WISE_DEPOT_DOMAIN_TAG_H

#include <stdint.h>

/** 单标签的 EPC/TID 最大字节数（与 UHF 实际帧上限一致） */
#define WD_TAG_EPC_MAX_LEN  32
#define WD_TAG_TID_MAX_LEN  32
#define WD_TAG_USER_MAX_LEN 64

/**
 * RFID 标签值对象（EPC/TID/用户区 + 信号强度）
 *
 * 说明：字段沿用历史命名 `epc/tid/user_data`，以便驱动层与业务层共用同一结构，
 * 不再各自定义一份。
 */
typedef struct {
    uint8_t epc[WD_TAG_EPC_MAX_LEN];   /**< EPC 原始字节 */
    uint8_t epc_len;                   /**< EPC 实际长度（字节） */
    uint8_t tid[WD_TAG_TID_MAX_LEN];   /**< TID 原始字节 */
    uint8_t tid_len;                   /**< TID 实际长度（字节） */
    uint8_t user_data[WD_TAG_USER_MAX_LEN]; /**< 用户区 */
    uint8_t user_len;                  /**< 用户区实际长度 */
    int8_t rssi;                       /**< 信号强度（如驱动提供） */
} rfid_tag_t;

#endif // WISE_DEPOT_DOMAIN_TAG_H
