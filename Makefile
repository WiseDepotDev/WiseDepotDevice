# Makefile for WiseDepot Device Client
# 遵循 POSIX.1-2017 标准，仅依赖 Linux 系统调用与标准库

CC = gcc
CFLAGS_COMMON = -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Iinclude $(shell pkg-config --cflags libcjson libcurl openssl)
LDFLAGS = $(shell pkg-config --libs libcjson libcurl openssl) -lpthread -lpaho-mqtt3a

# 调试与发布模式标志
CFLAGS_DEBUG = $(CFLAGS_COMMON) -g -O0 -DDEBUG
CFLAGS_RELEASE = $(CFLAGS_COMMON) -O2 -DNDEBUG

# 源码与对象文件
# 自动查找 src 下各模块的 .c 文件
SRC_DIRS = src/common src/infrastructure src/application src/domain
SRCS = $(wildcard src/*.c) $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
OBJS = $(patsubst src/%.c,obj/%.o,$(SRCS))

# 测试源码与对象
TEST_SRCS = $(wildcard test/*.c)
TEST_OBJS = $(patsubst test/%.c,obj/test/%.o,$(TEST_SRCS))
# 排除 main.o 以便链接测试运行器
LIB_OBJS = $(filter-out obj/main.o,$(OBJS))

# 输出目标
TARGET = bin/wise-device
TEST_TARGET = bin/test_runner

.PHONY: all debug release clean check test check-asan check-tsan check-mqtt directories coverage

all: debug

# 调试构建
debug: CFLAGS = $(CFLAGS_DEBUG)
debug: directories $(TARGET)

# 发布构建
release: CFLAGS = $(CFLAGS_RELEASE)
release: directories $(TARGET)

# 创建目录
directories:
	@mkdir -p bin obj/common obj/infrastructure obj/application obj/domain obj/test coverage

# 链接主程序
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译源码
obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# 编译测试代码
obj/test/%.o: test/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

# 运行测试
check: CFLAGS = $(CFLAGS_DEBUG)
check: directories $(LIB_OBJS) $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $(TEST_TARGET) $(TEST_OBJS) $(LIB_OBJS) $(LDFLAGS)
	./$(TEST_TARGET)

# 运行测试（check 的别名，便于 make test / make check 两种习惯）
test: check

# 内存安全检查 (P4-01：ASan + UBSan；需要 libasan/libubsan)
# 注意：**必须自己展开 check 的配方**，不能写成 `check-asan: clean check`——
# 那样 CFLAGS 会被 check 自己的 `check: CFLAGS = $(CFLAGS_DEBUG)` 覆盖，
# 结果是"只有链接带 sanitizer、对象文件没有 instrument"（P4-06 实测发现：
# 未被 instrument 时 __SANITIZE_ADDRESS__ 未定义，连测试里的 sanitizer 分支都会被漏掉）。
check-asan: CFLAGS = $(CFLAGS_COMMON) -g -O0 -DDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer
check-asan: LDFLAGS += -fsanitize=address,undefined
check-asan: clean directories $(LIB_OBJS) $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $(TEST_TARGET) $(TEST_OBJS) $(LIB_OBJS) $(LDFLAGS)
	./$(TEST_TARGET)

# 线程安全检查 (P4-03：TSan；需要 libtsan)
check-tsan: CFLAGS = $(CFLAGS_COMMON) -g -O0 -DDEBUG -fsanitize=thread -fno-omit-frame-pointer
check-tsan: LDFLAGS += -fsanitize=thread
check-tsan: clean directories $(LIB_OBJS) $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $(TEST_TARGET) $(TEST_OBJS) $(LIB_OBJS) $(LDFLAGS)
	./$(TEST_TARGET)

# MQTT 接收通道集成测试 (P4-05：连接真实 broker，需要 mosquitto；不挂在 check 里)
check-mqtt: CFLAGS = $(CFLAGS_DEBUG)
check-mqtt: directories $(LIB_OBJS)
	$(CC) $(CFLAGS) -o bin/mqtt_integration test/integration/mqtt_integration.c $(LIB_OBJS) $(LDFLAGS)
	bash test/integration/run_mqtt_integration.sh

# 代码覆盖率 (需要 lcov)
coverage: CFLAGS = $(CFLAGS_DEBUG) --coverage
coverage: LDFLAGS += --coverage
coverage: clean check
	lcov --capture --directory . --output-file coverage/coverage.info
	lcov --remove coverage/coverage.info '/usr/*' 'test/*' --output-file coverage/coverage_clean.info
	genhtml coverage/coverage_clean.info --output-directory coverage/report
	@echo "Coverage report generated in coverage/report/index.html"

# 清理
clean:
	rm -rf bin obj coverage *.gcno *.gcda

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
