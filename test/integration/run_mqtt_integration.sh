#!/bin/bash
# MQTT 接收通道集成用例的编排脚本（P4-05）
#
# 依赖：mosquitto（dnf install mosquitto）
# 行为：
#   阶段 1：起本地 broker（127.0.0.1:$PORT，allow_anonymous）→ 跑 mqtt_integration（订阅+发布+回收回调）
#   阶段 2：后台跑 mqtt_integration reconnect → 等它打印 READY_FOR_BROKER_KILL → 杀 broker
#           → 等 3s → 重启 broker → 等它自动重连并完成"重连后仍能收到消息"的校验
#
# 退出码：0 全绿；非 0 为失败（脚本自身或被测程序）

set -u
cd "$(dirname "$0")/../.." || exit 1

PORT="${MQTT_PORT:-18830}"
BINARY="bin/mqtt_integration"
CONF="$(mktemp /tmp/mosquitto-XXXX.conf)"
LOG="$(mktemp /tmp/mosquitto-XXXX.log)"
OUT="$(mktemp /tmp/mqtt-it-XXXX.out)"
BROKER_PID=""

cat > "$CONF" <<EOF
listener $PORT 127.0.0.1
allow_anonymous true
persistence false
EOF

cleanup() {
    if [ -n "$BROKER_PID" ] && kill -0 "$BROKER_PID" 2>/dev/null; then
        kill "$BROKER_PID" 2>/dev/null
        wait "$BROKER_PID" 2>/dev/null
    fi
    rm -f "$CONF" "$LOG" "$OUT"
}
trap cleanup EXIT

start_broker() {
    mosquitto -c "$CONF" >> "$LOG" 2>&1 &
    BROKER_PID=$!
    for _ in $(seq 1 50); do
        if ! kill -0 "$BROKER_PID" 2>/dev/null; then return 1; fi
        if (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then exec 3<&- ; return 0; fi
        sleep 0.1
    done
    return 1
}

stop_broker() {
    if [ -n "$BROKER_PID" ] && kill -0 "$BROKER_PID" 2>/dev/null; then
        kill "$BROKER_PID" 2>/dev/null
        wait "$BROKER_PID" 2>/dev/null
    fi
    BROKER_PID=""
}

if ! command -v mosquitto >/dev/null 2>&1; then
    echo "SKIP: 未安装 mosquitto（dnf install mosquitto）"
    exit 77
fi

echo "=== 阶段 1：单连接收发（订阅回调必须被触发） ==="
if ! start_broker; then
    echo "FAIL: broker 未能在 127.0.0.1:$PORT 启动"; tail -5 "$LOG"; exit 1
fi
if ! "$BINARY"; then
    echo "FAIL: 阶段 1"; exit 1
fi
stop_broker

echo
echo "=== 阶段 2：断线重连 + 订阅重放 ==="
if ! start_broker; then
    echo "FAIL: broker 重启失败"; tail -5 "$LOG"; exit 1
fi
"$BINARY" reconnect > "$OUT" 2>&1 &
IT_PID=$!

# 等被测程序说出"可以杀 broker 了"
for _ in $(seq 1 200); do
    if grep -q 'READY_FOR_BROKER_KILL' "$OUT" 2>/dev/null; then break; fi
    if ! kill -0 "$IT_PID" 2>/dev/null; then break; fi
    sleep 0.1
done
if ! grep -q 'READY_FOR_BROKER_KILL' "$OUT" 2>/dev/null; then
    echo "FAIL: 被测程序未进入断线阶段"; cat "$OUT"; wait "$IT_PID"; exit 1
fi

echo "  → 杀掉 broker，模拟断线"
stop_broker
sleep 3

echo "  → 重启 broker，等待自动重连与订阅重放"
if ! start_broker; then
    echo "FAIL: broker 第二次重启失败"; cat "$OUT"; exit 1
fi

wait "$IT_PID"
IT_RC=$?
cat "$OUT"
if [ "$IT_RC" -ne 0 ]; then
    echo "FAIL: 阶段 2（rc=$IT_RC）"; exit 1
fi

echo
echo "MQTT_INTEGRATION_ALL_OK"
