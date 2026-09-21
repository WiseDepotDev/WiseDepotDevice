# Makefile for WiseDepot Device Client
# 遵循 POSIX.1-2017 标准，仅依赖 Linux 系统调用与标准库
#
# 可移植性（P4-14）：
#   交叉编译   make release CROSS_COMPILE=aarch64-linux-gnu-
#   换编译器   make release CC=clang
#   换 pkg-config  make PKG_CONFIG=/usr/bin/pkg-config
# 依赖缺失时不再报一堆 "No such file or directory"，而是给出可操作的安装提示（见 check-deps）。

CROSS_COMPILE ?=
CC = $(CROSS_COMPILE)gcc
PKG_CONFIG ?= pkg-config

# 依赖清单（pkg-config 名 → 发行版包名提示）
PKGS = libcjson libcurl openssl
PKG_HINT_dnf = sudo dnf install -y cjson-devel libcurl-devel openssl-devel paho-c-devel
PKG_HINT_apt = sudo apt-get install -y libcjson-dev libcurl4-openssl-dev libssl-dev libpaho-mqtt-dev

CFLAGS_COMMON = -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Iinclude $(shell $(PKG_CONFIG) --cflags $(PKGS) 2>/dev/null)
LDFLAGS = $(shell $(PKG_CONFIG) --libs $(PKGS) 2>/dev/null) -lpthread -lpaho-mqtt3a

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

.PHONY: all debug release clean check test check-all check-asan check-tsan check-mqtt check-layers check-hygiene check-deps directories coverage install uninstall format format-check lint help

all: debug

# ---- 依赖自检（P4-14）：缺什么、装什么、装完怎么验证，一次说清 ----
check-deps:
	@miss=0; \
	if ! command -v $(PKG_CONFIG) >/dev/null 2>&1; then \
	  echo "缺失: pkg-config"; \
	  echo "  Fedora: sudo dnf install -y pkgconf-pkg-config"; \
	  echo "  Debian: sudo apt-get install -y pkg-config"; \
	  miss=1; \
	fi; \
	for pkg in $(PKGS); do \
	  if ! $(PKG_CONFIG) --exists $$pkg 2>/dev/null; then \
	    echo "缺失: $$pkg（pkg-config 名）"; miss=1; \
	  fi; \
	done; \
	if ! echo 'int main(void){return 0;}' | $(CC) -x c - -o /dev/null -lpaho-mqtt3a >/dev/null 2>&1; then \
	  echo "缺失: libpaho-mqtt3a（paho MQTT C 客户端）"; miss=1; \
	fi; \
	if [ $$miss -eq 1 ]; then \
	  echo ""; \
	  echo "安装提示（本机实测可用）："; \
	  echo "  $(PKG_HINT_dnf)"; \
	  echo "  $(PKG_HINT_apt)"; \
	  echo "装完用 make check-deps 复验。"; \
	  exit 1; \
	fi; \
	echo "依赖自检 OK: $(PKGS) + libpaho-mqtt3a 均可用（pkg-config: $(PKG_CONFIG)，CC: $(CC)）"

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
check-all: check check-layers check-hygiene check-asan check-tsan

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

# 注释与日志文案卫生（P4-13/P4-14）
# 背景：P4-10 批次3 的类型重命名把正则套在了字符串字面量上，'Config service initialized'
# 被改成 'wd_config_t service initialized'；当时的"机械等价性证明"用的是同一套映射，
# 按构造必然一致，发现不了这一类越界，因此补一条可执行门禁。
check-hygiene:
	@python3 tools/check-hygiene.py

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

# ---- 代码风格与静态检查（P4-14）----
CLANG_FORMAT ?= clang-format
CLANG_TIDY ?= clang-tidy
FORMAT_DIRS = src include test
FORMAT_FILES = $(shell find $(FORMAT_DIRS) -name '*.c' -o -name '*.h' 2>/dev/null)
# clang-tidy 需要编译数据库；没有 compile_commands.json 时用 -Iinclude 兜底并显式传编译选项
TIDY_FLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Iinclude $(shell $(PKG_CONFIG) --cflags $(PKGS) 2>/dev/null)

format:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { \
	  echo "缺失: $(CLANG_FORMAT)"; \
	  echo "  Fedora: sudo dnf install -y clang-tools-extra"; \
	  echo "  Debian: sudo apt-get install -y clang-format"; exit 1; }
	$(CLANG_FORMAT) -i $(FORMAT_FILES)
	@echo "clang-format 已就地格式化 $(words $(FORMAT_FILES)) 个文件"

format-check:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { \
	  echo "缺失: $(CLANG_FORMAT)（提示见 make format）"; exit 1; }
	$(CLANG_FORMAT) --dry-run -Werror $(FORMAT_FILES)

lint:
	@command -v $(CLANG_TIDY) >/dev/null 2>&1 || { \
	  echo "缺失: $(CLANG_TIDY)"; \
	  echo "  Fedora: sudo dnf install -y clang clang-tools-extra"; \
	  echo "  Debian: sudo apt-get install -y clang clang-tidy"; exit 1; }
	@echo "clang-tidy: $(words $(FORMAT_FILES)) 个文件（配置见 .clang-tidy）"
	@$(CLANG_TIDY) $(FORMAT_FILES) -- $(TIDY_FLAGS) 2>&1 | tee /tmp/wise-tidy.log | tail -5
	@if grep -qE 'error:' /tmp/wise-tidy.log; then \
	  echo "clang-tidy 存在 error 级问题："; grep -E 'error:' /tmp/wise-tidy.log | head -20; exit 1; \
	fi
	@echo "clang-tidy warning 数: $$(grep -cE 'warning:' /tmp/wise-tidy.log || true)（本门禁只阻断 error）"

help:
	@echo "WiseDepot 设备端构建目标："
	@echo "  make check-deps    依赖自检（缺什么/装什么）"
	@echo "  make debug|release 调试/发布构建（支持 CROSS_COMPILE=、CC=）"
	@echo "  make check         单元测试；check-all = check+layers+hygiene+asan+tsan"
	@echo "  make check-asan    ASan+UBSan；check-tsan = TSan；check-mqtt = 真实 broker 集成"
	@echo "  make check-hygiene 日志文案/独白注释/Doxygen 门禁；check-layers 分层围栏"
	@echo "  make format        clang-format 就地格式化；format-check 只校验"
	@echo "  make lint          clang-tidy（仅 error 阻断）"
	@echo "  make coverage      lcov 覆盖率；install/uninstall 安装到 DESTDIR"

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
