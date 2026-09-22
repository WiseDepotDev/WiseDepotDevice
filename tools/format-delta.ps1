# 格式债"不新增"检查（决策 9B 的配套机制；PowerShell 版，适配本机环境）
#
# 为什么要有 PowerShell 版：本机的 **git 在 Windows 侧**，而 **clang-format 在 WSL 里**，
# 两边各有一半工具，纯 .sh 版在 WSL 里跑不了（WSL 无 git）。本脚本用 Windows git 取文件清单，
# 用 WSL 的 clang-format 计数，从而在本机真实可跑。
#
# 口径：本次改动/新增的 .c/.h 文件，违规数不得比 HEAD 版本更多（全仓存量是咨询项）。
# HEAD 版本会被导出到**设备端仓库内部**的临时目录（.tmp-format-delta/），
# 这样 clang-format 才能按同一套 .clang-format 规则解析（仓库外解析不到会误报上千条）。
#
# 用法：powershell -NoProfile -File tools/format-delta.ps1
# 退出码：0 = 没有新增债务；1 = 有文件新增了违规（咨询项，可由人判断）
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$git = 'D:\Git\cmd\git.exe'
$wslDistro = 'Fedora-Mx'
$base = if ($args.Count -ge 1) { $args[0] } else { 'HEAD' }

Push-Location $root
try {
    $changed = @()
    $changed += & $git diff --name-only --diff-filter=ACMR $base -- '*.c' '*.h' 2>$null
    $changed += & $git ls-files --others --exclude-standard -- '*.c' '*.h' 2>$null
    $changed = $changed | Where-Object { $_ } | Sort-Object -Unique
    if ($changed.Count -eq 0) {
        Write-Host "format-delta: 相对 $base 没有改动过的 .c/.h 文件"
        exit 0
    }

    $tmp = Join-Path $root '.tmp-format-delta'
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null

    # 把仓库路径转成 WSL 路径（E:\a\b -> /mnt/e/a/b）
    function To-Wsl([string]$p) {
        $abs = $p -replace '\\', '/'
        if ($abs -match '^([A-Za-z]):/(.*)$') { return ('/mnt/' + $Matches[1].ToLower() + '/' + $Matches[2]) }
        return $abs
    }
    function Count-Violations([string]$wslPath, [string]$assume) {
        $cmd = "cd '$(To-Wsl $root)' && clang-format --dry-run -Werror --assume-filename='$assume' '$wslPath' 2>&1 | grep -c 'clang-format-violations' || true"
        $out = & wsl.exe -d $wslDistro -e bash -lc $cmd
        return [int]($out | Select-Object -Last 1)
    }

    Write-Host ('{0,-46} {1,8} {2,8}' -f '文件', $base, '当前')
    $debt = 0
    foreach ($f in $changed) {
        $rel = $f -replace '\\', '/'
        $cur = Count-Violations (To-Wsl (Join-Path $root $f)) $rel

        $baseCount = '-'
        $safe = ($f -replace '[\\/]', '_')
        $tmpFile = Join-Path $tmp $safe
        $has = & $git cat-file -e "$base`:$rel" 2>$null; $ok = $LASTEXITCODE -eq 0
        if ($ok) {
            & $git show "$base`:$rel" | Set-Content -Path $tmpFile -Encoding UTF8
            $baseCount = Count-Violations (To-Wsl $tmpFile) $rel
        }
        Write-Host ('{0,-46} {1,8} {2,8}' -f $rel, $baseCount, $cur)
        if ($baseCount -ne '-' -and $cur -gt $baseCount) { $debt++ }
    }

    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    if ($debt -eq 0) {
        Write-Host 'format-delta OK: 本次改动的文件没有新增格式违规（全仓存量仍是咨询项）'
        exit 0
    }
    Write-Host "format-delta 提示: $debt 个文件比 $base 多出格式违规——请把新增行按 .clang-format 调整，或说明理由（咨询项，不阻断）"
    exit 1
}
finally {
    Pop-Location
}
