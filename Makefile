# Makefile for WiseDepot Device Client
# 遵循 POSIX.1-2017 标准，仅依赖 Linux 系统调用与标准库

CC = gcc
CFLAGS_COMMON = -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Iinclude $(shell pkg-config --cflags libcjson libcurl openssl)
LDFLAGS = $(shell pkg-config --libs libcjson libcurl openssl) -lpthread -lpaho-mqtt3a

# 调试与发布模式标志
CFLAGS_DEBUG = $(CFLAGS_COMMON) -g -O0 -DDEBUG
CFLAGS_RELEASE = $(CFLAGS_COMMON) -O2 -DNDEBUG

# 对象目录：**每个编译配置一份**（P4-14）。
# 原先所有配置共用 obj/，而 make 只按时间戳判断是否重编，于是
# `make check-tsan && make release` 会拿 TSan 插桩的 .o 去链接普通程序并报
# `undefined reference to __tsan_read8`（P4-10 批次2b 复现），
# 同理 `make debug && make release` 的"release"其实链接的是 -O0 对象。
OBJROOT = obj
OBJDIRS = obj obj-release obj-asan obj-tsan obj-coverage

# 源码与对象文件（自动查找 src 下各模块的 .c 文件）
SRC_DIRS = src/common src/infrastructure src/application src/domain
SRCS = $(wildcard src/*.c) $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
OBJS = $(patsubst src/%.c,$(OBJROOT)/%.o,$(SRCS))

# 测试源码与对象
TEST_SRCS = $(wildcard test/*.c)
TEST_OBJS = $(patsubst test/%.c,$(OBJROOT)/test/%.o,$(TEST_SRCS))
# 排除 main.o 以便链接测试运行器
LIB_OBJS = $(filter-out $(OBJROOT)/main.o,$(OBJS))

# 输出目标
TARGET = bin/wise-device
TEST_TARGET = bin/test_runner

# 先决条件中的 $(OBJS)/$(LIB_OBJS)/$(TEST_OBJS) 依赖各目标的 OBJROOT，
# 必须在"目标特定变量"生效后再展开，因此开启二次展开（下面写成 $$(...)）。
.SECONDEXPANSION:

.PHONY: all debug release clean check test check-all check-asan check-tsan check-mqtt check-layers directories coverage install uninstall

all: debug

# 调试构建
debug: CFLAGS = $(CFLAGS_DEBUG)
debug: OBJROOT = obj
debug: directories $$(TARGET)

# 发布构建
release: CFLAGS = $(CFLAGS_RELEASE)
release: OBJROOT = obj-release
release: directories $$(TARGET)

# 创建目录
directories:
	@mkdir -p bin $(OBJDIRS) $(OBJROOT)/common $(OBJROOT)/infrastructure $(OBJROOT)/application $(OBJROOT)/domain $(OBJROOT)/test coverage

# 链接主程序
$(TARGET): $$(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 每个配置目录一套编译规则（模式规则里的 $(OBJROOT) 在解析期就展开了，
# 因此这里为 OBJDIRS 中每个目录各生成一份规则）。
define COMPILE_SRC_RULE
$(1)/%.o: src/%.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CFLAGS) -c -o $$@ $$<
endef

define COMPILE_TEST_RULE
$(1)/test/%.o: test/%.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CFLAGS) -Isrc -c -o $$@ $$<
endef

$(foreach d,$(OBJDIRS),$(eval $(call COMPILE_SRC_RULE,$(d))))
$(foreach d,$(OBJDIRS),$(eval $(call COMPILE_TEST_RULE,$(d))))

# 运行测试
check: CFLAGS = $(CFLAGS_DEBUG)
check: OBJROOT = obj
check: directories $$(LIB_OBJS) $$(TEST_OBJS)
	$(CC) $(CFLAGS) -o $(TEST_TARGET) $(TEST_OBJS) $(LIB_OBJS) $(LDFLAGS)
	./$(TEST_TARGET)

# 运行测试（check 的别名，便于 make test / make check 两种习惯）
test: check

# 本地一把梭门禁（不含需要 broker 的 check-mqtt）
check-all: check check-layers check-asan check-tsan

# 内存安全检查 (P4-01：ASan + UBSan；需要 libasan/libubsan)
# 注意：**必须自己展开 check 的配方**，不能写成 `check-asan: clean check`——
# 那样 CFLAGS 会被 check 自己的 `check: CFLAGS = $(CFLAGS_DEBUG)` 覆盖，
# 结果是"只有链接带 sanitizer、对象文件没有 instrument"（P4-06 实测发现：
# 未被 instrument 时 __SANITIZE_ADDRESS__ 未定义，连测试里的 sanitizer 分支都会被漏掉）。
check-asan: CFLAGS = $(CFLAGS_COMMON) -g -O0 -DDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer
check-asan: LDFLAGS += -fsanitize=address,undefined
check-asan: OBJROOT = obj-asan
check-asan: directories $$(LIB_OBJS) $$(TEST_OBJS)
	$(CC) $(CFLAGS) -o $(TEST_TARGET) $(TEST_OBJS) $(LIB_OBJS) $(LDFLAGS)
	./$(TEST_TARGET)

# 线程安全检查 (P4-03：TSan；需要 libtsan)
check-tsan: CFLAGS = $(CFLAGS_COMMON) -g -O0 -DDEBUG -fsanitize=thread -fno-omit-frame-pointer
check-tsan: LDFLAGS += -fsanitize=thread
check-tsan: OBJROOT = obj-tsan
check-tsan: directories $$(LIB_OBJS) $$(TEST_OBJS)
	$(CC) $(CFLAGS) -o $(TEST_TARGET) $(TEST_OBJS) $(LIB_OBJS) $(LDFLAGS)
	./$(TEST_TARGET)

# MQTT 接收通道集成测试 (P4-05：连接真实 broker，需要 mosquitto；不挂在 check 里)
check-mqtt: CFLAGS = $(CFLAGS_DEBUG)
check-mqtt: OBJROOT = obj
check-mqtt: directories $$(LIB_OBJS)
	$(CC) $(CFLAGS) -o bin/mqtt_integration test/integration/mqtt_integration.c $(LIB_OBJS) $(LDFLAGS)
	bash test/integration/run_mqtt_integration.sh

# 分层围栏（P4-08/P4-09）：用可执行的规则防止分层退化
# 六条已成立的约束：common↛infrastructure、common↛application、
# domain↛application、domain↛infrastructure、domain↛common/config.h、domain 不用 config_get()。
check-layers:
	@if grep -rn '#include "infrastructure/' src/common include/common; then echo "FAIL: common 层不得依赖 infrastructure"; exit 1; fi
	@if grep -rn '#include "application/' src/common include/common; then echo "FAIL: common 层不得依赖 application"; exit 1; fi
	@if grep -rn '#include "application/' src/domain include/domain; then echo "FAIL: domain 层不得依赖 application"; exit 1; fi
	@if grep -rn '#include "infrastructure/' src/domain include/domain; then echo "FAIL: domain 层不得依赖 infrastructure（P4-09）"; exit 1; fi
	@if grep -rn '#include "common/config.h"' src/domain include/domain; then echo "FAIL: domain 不得直接依赖 common/config.h（配置应由 application 注入，P4-09）"; exit 1; fi
	@if grep -rn 'config_get()' src/domain include/domain; then echo "FAIL: domain 不得调用 config_get()（P4-09）"; exit 1; fi
	@echo "layer check OK: common->infra=0, common->app=0, domain->app=0, domain->infra=0, domain->config=0"

# 代码覆盖率 (需要 lcov)
coverage: CFLAGS = $(CFLAGS_DEBUG) --coverage
coverage: LDFLAGS += --coverage
coverage: OBJROOT = obj-coverage
coverage: directories $$(LIB_OBJS) $$(TEST_OBJS)
	$(CC) $(CFLAGS) -o $(TEST_TARGET) $(TEST_OBJS) $(LIB_OBJS) $(LDFLAGS)
	./$(TEST_TARGET)
	lcov --capture --directory $(OBJROOT) --output-file coverage/coverage.info
	lcov --remove coverage/coverage.info '/usr/*' 'test/*' --output-file coverage/coverage_clean.info
	genhtml coverage/coverage_clean.info --output-directory coverage/report
	@echo "Coverage report generated in coverage/report/index.html"

# 清理
clean:
	rm -rf bin $(OBJDIRS) coverage *.gcno *.gcda

# 安装 (Release)
install: release
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/wise-device
	install -d $(DESTDIR)/etc/wise-device
	install -m 600 wise-device.conf.example $(DESTDIR)/etc/wise-device/wise-device.conf
	install -d $(DESTDIR)/usr/lib/systemd/system
	install -m 644 wise-device.service $(DESTDIR)/usr/lib/systemd/system/wise-device.service

# 卸载
uninstall:
	rm -f $(DESTDIR)/usr/local/bin/wise-device
	rm -f $(DESTDIR)/usr/lib/systemd/system/wise-device.service
	rm -rf $(DESTDIR)/etc/wise-device
