/**
 * @file wd_error.c
 * @brief 设备端统一错误码的名称映射（STD-CODE-06）。
 */

#include "common/wd_error.h"

const char *wd_error_str(wd_error_t err) {
    switch (err) {
        case WD_OK:              return "WD_OK";
        case WD_ERR_PARAM:       return "WD_ERR_PARAM";
        case WD_ERR_TIMEOUT:     return "WD_ERR_TIMEOUT";
        case WD_ERR_PROTOCOL:    return "WD_ERR_PROTOCOL";
        case WD_ERR_CRC:         return "WD_ERR_CRC";
        case WD_ERR_IO:          return "WD_ERR_IO";
        case WD_ERR_NOMEM:       return "WD_ERR_NOMEM";
        case WD_ERR_STATE:       return "WD_ERR_STATE";
        case WD_ERR_NOT_FOUND:   return "WD_ERR_NOT_FOUND";
        case WD_ERR_BUSY:        return "WD_ERR_BUSY";
        case WD_ERR_FULL:        return "WD_ERR_FULL";
        case WD_ERR_UNSUPPORTED: return "WD_ERR_UNSUPPORTED";
        case WD_ERR_CRYPTO:      return "WD_ERR_CRYPTO";
        case WD_ERR_CONNECT:     return "WD_ERR_CONNECT";
        case WD_ERR_CANCELED:    return "WD_ERR_CANCELED";
        case WD_ERR_ACTION:      return "WD_ERR_ACTION";
        case WD_ERR_SERVER:      return "WD_ERR_SERVER";
        case WD_ERR_AUTH:        return "WD_ERR_AUTH";
        case WD_ERR_GENERAL:     return "WD_ERR_GENERAL";
        default:                 return "WD_ERR_UNKNOWN";
    }
}
