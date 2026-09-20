/**
 * @file test_logger_concurrency.c
 * @brief 日志并发安全用例（P4-03）。
 *
 * 覆盖两点验收：
 * 1. 多线程压测无撕裂日志——每行必须是完整的一行（行首 '[', 含 "[INFO ] [" 与业务标记, 行尾 '\n'）；
 * 2. 轮转后日志不丢失——模拟 log_service 的 rename + logger_reopen(NULL)，
 *    轮转前的内容留在被重命名的文件里，之后的内容写进新文件，两边都不缺行。
 *
 * 说明：用例会把 stdout 暂时重定向到 /dev/null（日志同时输出到控制台，否则测试输出被 1600 行淹没），
 * 断言在恢复 stdout 之后执行，避免 unity 的 longjmp 把重定向留在进程里。
 */

#include "unity.h"
#include "common/logger.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CONC_THREADS 8
#define CONC_PER_THREAD 200
#define ROTATE_LINES 50

#define CONC_FILE "test_logger_concurrency.log"
#define ROTATE_FILE "test_logger_rotate.log"
#define ROTATED_FILE "test_logger_rotate.rotated.log"

static int g_saved_stdout = -1;

static void quiet_stdout(void) {
    fflush(stdout);
    g_saved_stdout = dup(fileno(stdout));
    FILE *devnull = freopen("/dev/null", "w", stdout);
    (void)devnull;
}

static void restore_stdout(void) {
    fflush(stdout);
    if (g_saved_stdout >= 0) {
        dup2(g_saved_stdout, fileno(stdout));
        close(g_saved_stdout);
        g_saved_stdout = -1;
    }
}

typedef struct {
    int total;
    int torn;
} log_scan_t;

/** 统计行数，并检查是否存在"撕裂/不完整"的行 */
static log_scan_t scan_log_file(const char *path) {
    log_scan_t result = {0, 0};
    FILE *fp = fopen(path, "r");
    if (!fp) return result;

    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        result.total++;
        size_t len = strlen(line);
        int complete = (line[0] == '[') &&
                       (strstr(line, "] [INFO ] [") != NULL) &&
                       (len > 0 && line[len - 1] == '\n');
        if (!complete) result.torn++;
    }
    fclose(fp);
    return result;
}

static void *log_worker(void *arg) {
    long tid = (long)arg;
    for (int i = 0; i < CONC_PER_THREAD; i++) {
        /* 内容唯一，避免被 60s 去重窗口抑制 */
        LOG_INFO("concurrency-probe tid=%ld seq=%d", tid, i);
    }
    return NULL;
}

/** 多线程写日志：行数完整、无撕裂 */
void test_logger_multithreaded_output_is_intact(void) {
    unlink(CONC_FILE);
    quiet_stdout();

    int init_rc = logger_init(CONC_FILE, LOG_LEVEL_INFO);
    pthread_t th[CONC_THREADS];
    int create_rc = 0;
    if (init_rc == 0) {
        for (long i = 0; i < CONC_THREADS; i++) {
            if (pthread_create(&th[i], NULL, log_worker, (void *)i) != 0) {
                create_rc = -1;
                break;
            }
        }
        for (int i = 0; i < CONC_THREADS; i++) {
            pthread_join(th[i], NULL);
        }
    }
    logger_close();

    log_scan_t scan = scan_log_file(CONC_FILE);
    restore_stdout();
    unlink(CONC_FILE);

    TEST_ASSERT_EQUAL(0, init_rc);
    TEST_ASSERT_EQUAL(0, create_rc);
    TEST_ASSERT_EQUAL(0, scan.torn);
    TEST_ASSERT_EQUAL(CONC_THREADS * CONC_PER_THREAD, scan.total);
}

/** 轮转（rename + reopen）后两侧日志都不缺行 */
void test_logger_reopen_keeps_logging(void) {
    unlink(ROTATE_FILE);
    unlink(ROTATED_FILE);
    quiet_stdout();

    int init_rc = logger_init(ROTATE_FILE, LOG_LEVEL_INFO);
    for (int i = 0; i < ROTATE_LINES; i++) {
        LOG_INFO("pre-rotate %d", i);
    }

    int rename_rc = rename(ROTATE_FILE, ROTATED_FILE);
    int reopen_rc = logger_reopen(NULL); /* log_service 的轮转路径 */

    for (int i = 0; i < ROTATE_LINES; i++) {
        LOG_INFO("post-rotate %d", i);
    }
    logger_close();

    log_scan_t old_file = scan_log_file(ROTATED_FILE);
    log_scan_t new_file = scan_log_file(ROTATE_FILE);
    restore_stdout();
    unlink(ROTATE_FILE);
    unlink(ROTATED_FILE);

    TEST_ASSERT_EQUAL(0, init_rc);
    TEST_ASSERT_EQUAL(0, rename_rc);
    TEST_ASSERT_EQUAL(0, reopen_rc);
    TEST_ASSERT_EQUAL(0, old_file.torn);
    TEST_ASSERT_EQUAL(0, new_file.torn);
    TEST_ASSERT_EQUAL(ROTATE_LINES, old_file.total);
    TEST_ASSERT_EQUAL(ROTATE_LINES, new_file.total);
}
