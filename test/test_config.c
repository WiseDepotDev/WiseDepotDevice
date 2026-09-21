#include "unity.h"
#include "common/config.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

void test_config(void) {
    // 1. Test defaults
    // 注意：config_load() 只做本地 I/O（环境变量 + 可选文件），不发起 HTTP，
    // 因此本用例可在无网络环境下运行。远端拉取是单独的 config_fetch_remote()。
    unsetenv("WISE_SERVER_URL");
    unsetenv("WISE_DEVICE_HEARTBEAT");
    config_load(NULL);
    const wd_config_t *cfg = config_get();
    assert(cfg != NULL);
    assert(cfg->server_url != NULL);
    assert(strcmp(cfg->server_url, "http://localhost:8080") == 0);
    // 默认心跳 1s：与服务端下发的配置一致（DeviceApplicationService 中 heartbeatInterval=1）
    assert(cfg->heartbeat_interval == 1);

    // 2. Test file loading
    // Create temporary config file
    const char *tmp_file = "test_config.tmp";
    FILE *fp = fopen(tmp_file, "w");
    assert(fp != NULL);
    fprintf(fp, "server_url=http://test-server:9000\n");
    fprintf(fp, "heartbeat_interval=60\n");
    fclose(fp);

    config_load(tmp_file);
    cfg = config_get();
    assert(strcmp(cfg->server_url, "http://test-server:9000") == 0);
    assert(cfg->heartbeat_interval == 60);

    unlink(tmp_file);

    // 3. Test Env Override
    setenv("WISE_DEVICE_HEARTBEAT", "120", 1);
    config_load(NULL); // Should respect env even if file is NULL
    cfg = config_get();
    assert(cfg->heartbeat_interval == 120);

    // Clean up
    unsetenv("WISE_DEVICE_HEARTBEAT");
    config_free();
}

/**
 * P4-04：请求签名密钥的来源与优先级
 * 环境变量 WISE_API_SIGNATURE_SECRET > 配置文件 signature_secret > NULL（源码内无默认值）
 */
void test_config_signature_secret(void) {
    const char *tmp_file = "test_config_secret.tmp";

    /* a) 都未配置 → NULL（源码内不再保留任何默认密钥） */
    unsetenv("WISE_API_SIGNATURE_SECRET");
    config_free();
    config_load(NULL);
    assert(config_signature_secret() == NULL);

    /* b) 仅环境变量 */
    setenv("WISE_API_SIGNATURE_SECRET", "env-secret", 1);
    config_free();
    config_load(NULL);
    assert(config_signature_secret() != NULL);
    assert(strcmp(config_signature_secret(), "env-secret") == 0);

    /* c) 仅配置文件 */
    FILE *fp = fopen(tmp_file, "w");
    assert(fp != NULL);
    fprintf(fp, "signature_secret=file-secret\n");
    fclose(fp);
    unsetenv("WISE_API_SIGNATURE_SECRET");
    config_free();
    config_load(tmp_file);
    assert(config_signature_secret() != NULL);
    assert(strcmp(config_signature_secret(), "file-secret") == 0);

    /* d) 两者同时存在 → 环境变量优先（与 config.h 的文档契约一致） */
    setenv("WISE_API_SIGNATURE_SECRET", "env-secret", 1);
    config_free();
    config_load(tmp_file);
    assert(config_signature_secret() != NULL);
    assert(strcmp(config_signature_secret(), "env-secret") == 0);

    unsetenv("WISE_API_SIGNATURE_SECRET");
    unlink(tmp_file);
    config_free();
}

/**
 * P4-04：环境变量 > 配置文件（此前实现是"文件后解析"，会覆盖环境变量，与文档契约相反）
 */
void test_config_env_overrides_file(void) {
    const char *tmp_file = "test_config_priority.tmp";

    FILE *fp = fopen(tmp_file, "w");
    assert(fp != NULL);
    fprintf(fp, "server_url=http://from-file:9000\n");
    fprintf(fp, "heartbeat_interval=60\n");
    fclose(fp);

    setenv("WISE_SERVER_URL", "http://from-env:8080", 1);
    setenv("WISE_DEVICE_HEARTBEAT", "99", 1);
    config_free();
    config_load(tmp_file);

    const wd_config_t *cfg = config_get();
    assert(cfg != NULL);
    assert(strcmp(cfg->server_url, "http://from-env:8080") == 0);
    assert(cfg->heartbeat_interval == 99);

    unsetenv("WISE_SERVER_URL");
    unsetenv("WISE_DEVICE_HEARTBEAT");
    unlink(tmp_file);
    config_free();
}


/** P4-17：持久化配置的读回（含"文件缺失/损坏不影响启动"两条兜底） */
void test_config_persistent_roundtrip(void) {
    /* 前提：测试运行器已把 CWD 切到临时目录，这里是干净起点 */
    remove("wise-device.dat");

    /* 1) 文件不存在时：读回应返回非零且不改动默认值 */
    config_free();
    config_load(NULL);
    const wd_config_t *cfg = config_get();
    TEST_ASSERT_NOT_NULL(cfg);
    TEST_ASSERT_TRUE(config_load_persistent() != WD_OK);
    TEST_ASSERT_EQUAL_STRING("http://localhost:8080", cfg->server_url);

    /* 2) 落盘 → 重新加载 → 值被读回（用没有 env 覆盖的字段验证，避免测试间互相影响） */
    cfg = config_get();
    ((wd_config_t *)cfg)->motor_trim_a = 0.42f;
    ((wd_config_t *)cfg)->task_poll_interval = 7;
    TEST_ASSERT_EQUAL(WD_OK, config_save_persistent());

    config_free();
    config_load(NULL);
    cfg = config_get();
    TEST_ASSERT_TRUE(cfg->motor_trim_a > 0.41f && cfg->motor_trim_a < 0.43f);
    TEST_ASSERT_EQUAL(7, cfg->task_poll_interval);

    /* 3) 文件损坏：不影响启动，沿用默认值 */
    FILE *fp = fopen("wise-device.dat", "w");
    TEST_ASSERT_NOT_NULL(fp);
    fputs("{ this is not json", fp);
    fclose(fp);

    config_free();
    config_load(NULL);
    cfg = config_get();
    TEST_ASSERT_NOT_NULL(cfg);
    TEST_ASSERT_EQUAL_STRING("http://localhost:8080", cfg->server_url);
    TEST_ASSERT_TRUE(cfg->move_speed_cm_s > 19.6f && cfg->move_speed_cm_s < 19.8f);

    remove("wise-device.dat");
}
