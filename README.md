# WiseDepotDevice

慧仓智控设备端：运行在 Linux 单板机上的 C11 程序（Raspberry Pi 5 等），负责设备注册与心跳、巡检任务执行、
运动控制、RFID 读头接入、日志上传与系统监控，通过统一信封协议与 `WiseDeoptServer` 通信。

## 技术栈

C11 / POSIX；仅依赖 `libcjson`、`libcurl`、`OpenSSL` 与 `paho-mqtt`。构建使用 Makefile（GCC 或 Clang）。

## 目录结构

```
src/common          基础能力：配置、日志、错误码、信封、调度、退避、加密、系统监控、内存封装
src/domain          业务模型：巡检任务与 JSON、运动控制、库存缓存
src/infrastructure  外部依赖实现：HTTP、MQTT、RFID 驱动、服务端发现
src/application     设备端编排：注册与鉴权、心跳、巡检、日志上传、MQTT 接入、运动与 RFID 服务
include/            与上述模块对应的头文件
test/               Unity 单元测试与集成测试
tools/              示例导入、格式化与卫生检查脚本
wise-device.conf.example   配置文件模板
wise-device.service        systemd 单元
```

## 环境要求

GCC 或 Clang、`make`、`pkg-config`，以及 cJSON / libcurl / OpenSSL / paho-mqtt 开发包。
依赖安装提示见 `make check-deps`（同时给出 dnf 与 apt 命令）。

## 构建与测试

```bash
make debug                 # 调试构建 → bin/wise-device
make release               # 发布构建（-O2）
make release CROSS_COMPILE=aarch64-linux-gnu-   # 交叉编译
make test                  # 单元测试与集成测试
make check-all             # 分层检查 + 卫生检查 + ASan/UBSan + TSan
```

每个编译配置使用独立的对象目录（`obj` / `obj-release` / `obj-asan` / `obj-tsan`），互不污染。

## 配置与部署

```bash
sudo install -d -m 755 /etc/wise-device
sudo install -m 600 wise-device.conf.example /etc/wise-device/wise-device.conf
sudo editor /etc/wise-device/wise-device.conf   # 至少填写 server_url / device_id / signature_secret
```

配置优先级为：环境变量 > 配置文件 > 源码默认值。签名密钥、MQTT 口令与加密密钥在源码内没有默认值，
缺失时对应能力会被显式拒绝。服务以 `wise-device.service` 部署，systemd 可用 `EnvironmentFile=` 注入敏感项。

## 许可证

本项目采用 **GPL-3.0** 许可证，详见 `LICENSE`。
