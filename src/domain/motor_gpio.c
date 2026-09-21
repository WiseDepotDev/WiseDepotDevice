/**
 * @file motor_gpio.c
 * @brief GPIO sysfs 低层操作（P4-10 从 motor_controller.c 拆出，仅搬不改）。
 */

#include "domain/motor_internal.h"
#include "common/logger.h"
#include "common/wd_error.h"
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
/* ==================== GPIO 内部函数实现 ==================== */

/**
 * 导出 GPIO 引脚
 */
int gpio_export(int *pin) {
    // 尝试导出指定的引脚
    // 如果失败且是 EINVAL，尝试加上 Pi 5 的偏移量 (571 或 569)
    
    int current_pin = *pin;
    int retry_pin = -1;
    
    // 偏移量数组
    int offsets[] = {RPI5_GPIO_OFFSET_1, RPI5_GPIO_OFFSET_2};
    
    // 第一次尝试：原始引脚
    for (int attempt = 0; attempt <= 2; attempt++) {
        if (attempt > 0) {
            // 后续尝试：尝试不同的偏移量
            // 如果原始引脚已经很大了，可能不需要偏移
            if (current_pin < 500) {
                retry_pin = current_pin + offsets[attempt-1];
                LOG_INFO("Retrying GPIO export with Pi 5 offset (%d): %d -> %d", 
                         offsets[attempt-1], current_pin, retry_pin);
                // 这里需要注意，如果是第二次重试（attempt=2），需要基于原始引脚
                // 但是我们在 attempt=1 时修改了 current_pin?
                // 让我们保持简单：总是基于原始输入 pin (保存在 *pin 中)
                current_pin = *pin + offsets[attempt-1];
            } else {
                break; 
            }
        }
        
        char pin_path[64];
        snprintf(pin_path, sizeof(pin_path), "/sys/class/gpio/gpio%d", current_pin);
        if (access(pin_path, F_OK) == 0) {
            /* Already exported */
            if (attempt > 0) {
                 *pin = current_pin; // Update the caller's pin value
            }
            return 0;
        }

        char path[64];
        snprintf(path, sizeof(path), "/sys/class/gpio/export");
        
        int fd = open(path, O_WRONLY);
        if (fd < 0) {
            LOG_ERROR("Failed to open GPIO export: %s (Try running as root?)", strerror(errno));
            return WD_ERR_IO;
        }
        
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", current_pin);
        if (write(fd, buf, strlen(buf)) < 0) {
            if (errno == EBUSY) {
                /* Already exported by someone else or kernel, assume OK */
                close(fd);
                if (attempt > 0) {
                     *pin = current_pin;
                }
                return 0;
            }
            
            // Only log error on last attempt
            if (attempt == 2) {
                if (errno == EINVAL) {
                    LOG_ERROR("Invalid GPIO pin %d. Check if the pin number is correct for your board.", current_pin);
                } else {
                    LOG_ERROR("Failed to write to GPIO export (%d): %s", current_pin, strerror(errno));
                }
            }
            close(fd);
            continue; // Try next attempt
        }
        
        close(fd);
        
        // Wait for udev to create the device node
        struct timespec ts2 = {0, 100000000}; /* 100ms */
        nanosleep(&ts2, NULL);
        
        if (attempt > 0) {
             *pin = current_pin; // Update successful pin
        }
        return 0;
    }
    
    return WD_ERR_IO;
}

/**
 * 设置 GPIO 方向
 */
int gpio_set_direction(int pin, bool output) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        LOG_ERROR("Failed to open GPIO direction (%s): %s", path, strerror(errno));
        return WD_ERR_IO;
    }
    
    const char *dir = output ? "out" : "in";
    if (write(fd, dir, strlen(dir)) < 0) {
        LOG_ERROR("Failed to write GPIO direction (%s): %s", path, strerror(errno));
        close(fd);
        return WD_ERR_IO;
    }
    
    close(fd);
    return 0;
}

/**
 * 写入 GPIO 值
 */
int gpio_write(int pin, bool value) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        LOG_ERROR("Failed to open GPIO value (%s): %s", path, strerror(errno));
        return WD_ERR_IO;
    }
    
    char buf[2] = {value ? '1' : '0', '\0'};
    if (write(fd, buf, 1) < 0) {
        LOG_ERROR("Failed to write GPIO value (%s): %s", path, strerror(errno));
        close(fd);
        return WD_ERR_IO;
    }
    
    // LOG_DEBUG("GPIO %d set to %d", pin, value);
    close(fd);
    return 0;
}

/**
 * 取消导出 GPIO 引脚
 */
int gpio_unexport(int pin) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/unexport");
    
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        return WD_ERR_IO;
    }
    
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", pin);
    if (write(fd, buf, strlen(buf)) < 0) {
        close(fd);
        return WD_ERR_IO;
    }
    
    close(fd);
    return 0;
}
