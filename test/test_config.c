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
    config_load(NULL);
    const Config *cfg = config_get();
    assert(cfg != NULL);
    assert(cfg->server_url != NULL);
    assert(strcmp(cfg->server_url, "http://localhost:8080") == 0);
    // 默认心跳 1s：与服务端下发的配置一致（DeviceApplicationService 中 heartbeatInterval=1）
    assert(cfg->heartbeat_interval == 1);
    
    // 2. Test file loading
    // Create temporary config file
    const char *tmp_file = "test_config.tmp";
    FILE *fp = fopen(tmp_file, "w");
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
