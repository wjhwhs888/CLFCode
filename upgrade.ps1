# CLFCode 升级脚本
# 用法: irm https://gitee.com/sherlock0923/CLFCode/raw/master/upgrade.ps1 | iex
#
# 保留用户配置，仅更新程序文件

$ErrorActionPreference = "Stop"

$REPO_OWNER = "sherlock0923"
$REPO_NAME  = "CLFCode"
$INSTALL_DIR = "$env:USERPROFILE\CLFCode"

Write-Host "● CLFCode 升级程序" -ForegroundColor Cyan

# ── 检查是否已安装 ──
if (-not (Test-Path "$INSTALL_DIR\bin\Release\CLFCode.exe")) {
    Write-Host "  CLFCode 未安装，请先运行安装脚本:" -ForegroundColor Yellow
    Write-Host "  irm https://gitee.com/$REPO_OWNER/$REPO_NAME/raw/master/install.ps1 | iex" -ForegroundColor White
    exit 1
}

# ── 获取最新版本 ──
Write-Host "  正在查询最新版本..." -ForegroundColor Gray
try {
    $versionUrl = "https://gitee.com/$REPO_OWNER/$REPO_NAME/raw/master/VERSION"
    $latestVersion = (Invoke-RestMethod -Uri $versionUrl -TimeoutSec 10).Trim()
} catch {
    Write-Host "  ✗ 无法获取版本信息" -ForegroundColor Red
    exit 1
}

# ── 获取当前版本 ──
$currentVersion = ""
$versionFile = "$INSTALL_DIR\VERSION"
if (Test-Path $versionFile) {
    $currentVersion = (Get-Content $versionFile).Trim()
}

if ($currentVersion -eq $latestVersion) {
    Write-Host ""
    Write-Host "✔ 已是最新版本 ($currentVersion)" -ForegroundColor Green
    return
}

Write-Host "  当前版本: $currentVersion  →  最新版本: $latestVersion" -ForegroundColor Yellow

# ── 备份用户配置 ──
$backupConfig = "$env:TEMP\CLFCode_config_backup"
$configDir = "$INSTALL_DIR\config"
if (Test-Path $configDir) {
    Copy-Item -Path $configDir -Destination $backupConfig -Recurse -Force
    Write-Host "  配置已备份" -ForegroundColor Gray
}

# ── 下载新版本 ──
$zipUrl = "https://gitee.com/$REPO_OWNER/$REPO_NAME/releases/download/$latestVersion/CLFCode-$latestVersion-win64.zip"
$zipPath = "$env:TEMP\CLFCode-$latestVersion-upgrade.zip"

Write-Host "  正在下载 $latestVersion..." -ForegroundColor Gray
try {
    Invoke-WebRequest -Uri $zipUrl -OutFile $zipPath -TimeoutSec 300
} catch {
    try {
        $releaseApi = "https://gitee.com/api/v5/repos/$REPO_OWNER/$REPO_NAME/releases/latest"
        $releaseInfo = Invoke-RestMethod -Uri $releaseApi -TimeoutSec 10
        $asset = $releaseInfo.assets | Where-Object { $_.name -like "*win64.zip" } | Select-Object -First 1
        if ($asset) {
            Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zipPath -TimeoutSec 300
        } else {
            throw "找不到发布包"
        }
    } catch {
        Write-Host "  ✗ 下载失败" -ForegroundColor Red
        exit 1
    }
}

# ── 替换安装 ──
Write-Host "  正在升级..." -ForegroundColor Gray
Remove-Item -Path $INSTALL_DIR -Recurse -Force -ErrorAction SilentlyContinue

$tempExtract = "$env:TEMP\CLFCode_extract"
if (Test-Path $tempExtract) { Remove-Item -Path $tempExtract -Recurse -Force }
Expand-Archive -Path $zipPath -DestinationPath $tempExtract -Force
Remove-Item -Path $zipPath -Force

$innerDir = Get-ChildItem -Path $tempExtract -Directory | Select-Object -First 1
if ($innerDir) {
    Move-Item -Path $innerDir.FullName -Destination $INSTALL_DIR -Force
} else {
    Move-Item -Path $tempExtract -Destination $INSTALL_DIR -Force
}
if (Test-Path $tempExtract) { Remove-Item -Path $tempExtract -Recurse -Force }

# 恢复配置
if ($backupConfig -and (Test-Path $backupConfig)) {
    New-Item -ItemType Directory -Path "$INSTALL_DIR\config" -Force | Out-Null
    Copy-Item -Path $backupConfig\* -Destination "$INSTALL_DIR\config\" -Recurse -Force
    Remove-Item -Path $backupConfig -Recurse -Force
    Write-Host "  配置已恢复" -ForegroundColor Green
}

# 确保 PATH
$binDir = "$INSTALL_DIR\bin\Release"
$currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($currentPath -notlike "*$binDir*") {
    [Environment]::SetEnvironmentVariable("Path", "$currentPath;$binDir", "User")
    $env:Path = "$env:Path;$binDir"
}

# ── 完成 ──
Write-Host ""
Write-Host "✔ CLFCode 已升级到 $latestVersion" -ForegroundColor Green
