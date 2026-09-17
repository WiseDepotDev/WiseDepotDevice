#include "common/config.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

void test_config(void) {
    // 1. Test defaults
    config_load(NULL);
    const Config *cfg = config_get();
    assert(cfg != NULL);
    assert(cfg->server_url != NULL);
    assert(strcmp(cfg->server_url, "http://localhost:8080") == 0);
    assert(cfg->heartbeat_interval == 30);
    
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
