# CLFCode 升级脚本（薄壳，2026-09-22 S5）
# 用法: irm https://gitee.com/sherlock0923/CLFCode/raw/master/upgrade.ps1 | iex
#
# 保留用户配置，仅更新程序文件。
# 唯一权威实现 = install.ps1（含占用探测/改名后删/嵌套防御/回滚）——
# 本薄壳只做：已装检查 + 版本比较 + 拉取 install.ps1 以 -Upgrade 执行。

$ErrorActionPreference = "Stop"

$REPO_OWNER = "sherlock0923"
$REPO_NAME  = "CLFCode"
$INSTALL_DIR = if ($env:CLFCODE_TEST_INSTALL_DIR) { $env:CLFCODE_TEST_INSTALL_DIR }
               else { "$env:USERPROFILE\CLFCode" }
$BASE = "https://gitee.com/$REPO_OWNER/$REPO_NAME/raw/master"
# 测试钩子（V3 验证用）：指向本地 install.ps1 副本（未设置时走远端 Gitee）
$INSTALL_URL = if ($env:CLFCODE_TEST_INSTALL_URL) { $env:CLFCODE_TEST_INSTALL_URL }
               else { "$BASE/install.ps1" }

Write-Host "● CLFCode 升级程序" -ForegroundColor Cyan

# ── 检查是否已安装 ──
if (-not (Test-Path "$INSTALL_DIR\bin\Release\CLFCode.exe")) {
    Write-Host "  CLFCode 未安装，请先运行安装脚本:" -ForegroundColor Yellow
    Write-Host "  irm $BASE/install.ps1 | iex" -ForegroundColor White
    exit 1
}

# ── 获取最新版本 ──
Write-Host "  正在查询最新版本..." -ForegroundColor Gray
try {
    $versionUrl = "$BASE/VERSION"
    $latestVersion = (Invoke-RestMethod -Uri $versionUrl -TimeoutSec 10).Trim()
} catch {
    Write-Host "  ✗ 无法获取版本信息" -ForegroundColor Red
    exit 1
}

# ── 获取当前版本 ──
$currentVersion = ""
$versionFile = "$INSTALL_DIR\VERSION"
if (Test-Path -LiteralPath $versionFile) {
    $currentVersion = (Get-Content $versionFile).Trim()
}

if ($currentVersion -eq $latestVersion) {
    Write-Host ""
    Write-Host "✔ 已是最新版本 ($currentVersion)" -ForegroundColor Green
    return
}

Write-Host "  当前版本: $currentVersion  →  最新版本: $latestVersion" -ForegroundColor Yellow
Write-Host ""

# ── 拉取 install.ps1 执行（唯一权威实现） ──
# 编码镜像约束：远端脚本为 irm|iex 兼容必须无 BOM；而 & 执行（-File 语义）
# 在 PS5.1 下需要 BOM（无 BOM 中文按 GBK 误读破坏解析）——统一转写为
# 带 BOM 临时文件后执行
$tmp = Join-Path $env:TEMP "CLFCode-install-$([guid]::NewGuid().ToString('N')).ps1"
$tmpBom = "$tmp.bom.ps1"
try {
    if ($env:CLFCODE_TEST_INSTALL_URL -and (Test-Path -LiteralPath $env:CLFCODE_TEST_INSTALL_URL)) {
        # 测试钩子：本地文件副本（验证未推送改动用）
        Copy-Item -Path $env:CLFCODE_TEST_INSTALL_URL -Destination $tmp -Force
    } else {
        Invoke-WebRequest -Uri $INSTALL_URL -OutFile $tmp -TimeoutSec 60
    }
    $content = [System.IO.File]::ReadAllText($tmp, [System.Text.Encoding]::UTF8)
    [System.IO.File]::WriteAllText($tmpBom, $content, [System.Text.UTF8Encoding]::new($true))
    & $tmpBom
    exit $LASTEXITCODE
} finally {
    Remove-Item -Path $tmp -Force -ErrorAction SilentlyContinue
    Remove-Item -Path $tmpBom -Force -ErrorAction SilentlyContinue
}
