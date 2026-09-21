#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# P4-13/P4-14 卫生门禁（由 make check-hygiene 调用）：
#   1. 日志/输出字符串里不得出现设备端类型名（P4-10 批次3 重命名曾污染 13 处日志文案）；
#   2. 源码里不得留下 AI 独白/推理过程注释（"让我们/假设是/我们需要/Let's/For now/..."）。
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TYPES = (
    'wd_config_t|scheduler_task_t|scheduler_task_callback_t|patrol_task_t|patrol_task_callback_t|'
    'patrol_task_status_t|patrol_task_type_t|patrol_action_t|patrol_action_type_t|patrol_point_t|'
    'patrol_service_config_t|motor_config_t|motor_id_t|motor_direction_t|move_direction_t|'
    'motor_controller_config_t|http_response_t|device_info_t|log_level_t|backoff_config_t|backoff_state_t'
)

STR_PAT = re.compile(r'"[^"\n]*(?:' + TYPES + r')\b')
MONOLOGUE = re.compile(
    r'让我们|假设是|我们需要|如何避免|我是 AI|先不要提交|'      # 中文独白
    r"Let's |For now|we might|We might|we need to|We need to|"
    r'Assume |assume the|Simplify|simplified for now|Continue\? Or fail|'
    r'或许|是不是应该|要不要'
)

BAD = []
for dp, _dn, fns in os.walk(ROOT):
    if '/obj' in dp or '/bin' in dp or '/.git' in dp:
        continue
    for fn in fns:
        if not fn.endswith(('.c', '.h')):
            continue
        p = os.path.join(dp, fn)
        rel = os.path.relpath(p, ROOT).replace('\\', '/')
        for i, line in enumerate(io.open(p, encoding='utf-8', errors='replace'), 1):
            if rel.startswith('test/'):
                only_mono = True
            else:
                only_mono = False
            if not only_mono and STR_PAT.search(line):
                BAD.append('%s:%d 日志字符串含类型名: %s' % (rel, i, line.strip()[:90]))
            stripped = line.strip()
            if stripped.startswith(('//', '*', '/*')) and MONOLOGUE.search(line):
                BAD.append('%s:%d 独白注释: %s' % (rel, i, stripped[:90]))

# 3) public 头文件的函数声明必须有 Doxygen（@brief）
DECL = re.compile(
    r'^\s*(?:static\s+)?(?:wd_error_t|void|int|bool|size_t|ssize_t|uint8_t|uint16_t|uint32_t|'
    r'const char \*|char \*|float|double|long|wd_envelope_result_t|log_level_t|http_response_t \*)'
    r'\s+(\w+)\s*\(')
for dp, _dn, fns in os.walk(os.path.join(ROOT, 'include')):
    for fn in fns:
        if not fn.endswith('.h'):
            continue
        p = os.path.join(dp, fn)
        rel = os.path.relpath(p, ROOT).replace('\\', '/')
        lines = io.open(p, encoding='utf-8', errors='replace').read().replace('\r\n', '\n').split('\n')
        for i, line in enumerate(lines):
            m = DECL.match(line)
            if not m or m.group(1) in ('if', 'for', 'while', 'switch', 'sizeof'):
                continue
            if '@brief' not in '\n'.join(lines[max(0, i - 14):i]):
                BAD.append('%s:%d 函数 %s 缺 @brief（P4-13）' % (rel, i + 1, m.group(1)))

if BAD:
    print('check-hygiene FAIL:')
    for b in BAD:
        print('  ' + b)
    sys.exit(1)
print('check-hygiene OK: 日志字符串无类型名残留；无独白注释；public 声明均有 @brief')

