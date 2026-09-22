#!/bin/sh
# 格式债"不新增"检查（决策 9B 的配套机制，咨询项）
#
# 背景：全仓 `make format-check` 长期为红（三配置实测 1720 / 3077 / 2012 处），
# 无法当作阻断门禁。折中口径是：**本次改动过的文件，违规数不得比 HEAD 版本更多**。
#
# 做法：对每个被改动/新增的 .c/.h 文件，分别统计
#   ① 当前工作区版本的违规条数
#   ② HEAD 版本的违规条数（git show 到临时目录；文件在仓库外时 clang-format 会找不到
#      .clang-format，因此用 --assume-filename 指回原路径）
# 逐项打印并给出结论。退出码：0 = 没有新增债务；1 = 有文件新增了违规（咨询项，可由人判断）。
#
# 用法：make format-delta  或  bash tools/format-delta.sh [基线 ref]（默认 HEAD）
set -u

BASE="${1:-HEAD}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 2

if ! command -v clang-format >/dev/null 2>&1; then
    echo "缺少 clang-format，跳过 format-delta"
    exit 0
fi

# 本机（Windows）的 git 在 Windows 侧，WSL 里没有 git —— 此时不能假装"没有改动"，
# 必须明说并指向 PowerShell 版（tools/format-delta.ps1，用 Windows git + WSL clang-format）。
if ! command -v git >/dev/null 2>&1; then
    echo "format-delta: 当前环境没有 git（本机 WSL 无 git）——请改用 PowerShell 版："
    echo "    powershell -NoProfile -File tools/format-delta.ps1"
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

count() { # $1=实际文件 $2=用于解析 .clang-format 的路径
    clang-format --dry-run -Werror --assume-filename="$2" "$1" 2>&1 | grep -c 'clang-format-violations'
}

CHANGED="$(git diff --name-only --diff-filter=ACMR "$BASE" -- '*.c' '*.h' 2>/dev/null; \
           git ls-files --others --exclude-standard -- '*.c' '*.h' 2>/dev/null)"
if [ -z "$CHANGED" ]; then
    echo "format-delta: 相对 $BASE 没有改动过的 .c/.h 文件"
    exit 0
fi

printf '%-46s %8s %8s\n' "文件" "$BASE" "当前"
worst=0
for f in $CHANGED; do
    [ -f "$f" ] || continue
    base_n="-"
    if git cat-file -e "$BASE:$f" 2>/dev/null; then
        safe="$(echo "$f" | tr '/' '_')"
        git show "$BASE:$f" > "$TMP/$safe" 2>/dev/null
        base_n="$(count "$TMP/$safe" "$f")"
    fi
    cur_n="$(count "$f" "$f")"
    printf '%-46s %8s %8s\n' "$f" "$base_n" "$cur_n"
    if [ "$base_n" != "-" ] && [ "$cur_n" -gt "$base_n" ]; then
        worst=1
    fi
done

if [ "$worst" -eq 0 ]; then
    echo "format-delta OK: 本次改动的文件没有新增格式违规（全仓存量仍是咨询项）"
    exit 0
fi
echo "format-delta 提示: 有文件比 $BASE 多出格式违规——请把新增行按 .clang-format 调整，或说明理由（咨询项，不阻断）"
exit 1
