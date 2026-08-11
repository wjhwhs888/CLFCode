# CLFCode Windows 安装脚本
# 用法: irm https://gitee.com/sherlock0923/CLFCode/raw/master/install.ps1 | iex
#
# 安装到 %USERPROFILE%\CLFCode，自动添加到用户 PATH，无需管理员权限

$ErrorActionPreference = "Stop"

$REPO_OWNER = "sherlock0923"
$REPO_NAME  = "CLFCode"
$INSTALL_DIR = "$env:USERPROFILE\CLFCode"

Write-Host "● CLFCode 安装程序" -ForegroundColor Cyan
Write-Host "  安装目录: $INSTALL_DIR"
Write-Host ""

# ── 1. 获取最新版本号 ──
Write-Host "  正在查询最新版本..." -ForegroundColor Gray
try {
    $versionUrl = "https://gitee.com/$REPO_OWNER/$REPO_NAME/raw/master/VERSION"
    $latestVersion = (Invoke-RestMethod -Uri $versionUrl -TimeoutSec 10).Trim()
    Write-Host "  最新版本: $latestVersion" -ForegroundColor Green
} catch {
    Write-Host "  ✗ 无法获取版本信息，请检查网络连接" -ForegroundColor Red
    exit 1
}

# ── 2. 下载发布包 ──
$zipUrl = "https://gitee.com/$REPO_OWNER/$REPO_NAME/releases/download/$latestVersion/CLFCode-$latestVersion-win64.zip"
$zipPath = "$env:TEMP\CLFCode-$latestVersion.zip"

Write-Host "  正在下载: $zipUrl" -ForegroundColor Gray
try {
    # 尝试 Gitee releases 直链
    Invoke-WebRequest -Uri $zipUrl -OutFile $zipPath -TimeoutSec 300
} catch {
    # 备用方案：尝试从 release 附件获取
    Write-Host "  直链下载失败，尝试备用方式..." -ForegroundColor Yellow
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
        Write-Host "  ✗ 下载失败。请手动下载并解压到: $INSTALL_DIR" -ForegroundColor Red
        Write-Host "  发布页: https://gitee.com/$REPO_OWNER/$REPO_NAME/releases" -ForegroundColor Yellow
        exit 1
    }
}
Write-Host "  下载完成 ($([math]::Round((Get-Item $zipPath).Length / 1MB, 1)) MB)" -ForegroundColor Green

# ── 3. 解压到安装目录 ──
Write-Host "  正在安装..." -ForegroundColor Gray
if (Test-Path $INSTALL_DIR) {
    # 保留用户配置
    $backupConfig = $null
    $configDir = "$INSTALL_DIR\config"
    if (Test-Path $configDir) {
        $backupConfig = "$env:TEMP\CLFCode_config_backup"
        Copy-Item -Path $configDir -Destination $backupConfig -Recurse -Force
    }
    Remove-Item -Path $INSTALL_DIR -Recurse -Force
}

Expand-Archive -Path $zipPath -DestinationPath $env:USERPROFILE -Force
Remove-Item -Path $zipPath -Force
Write-Host "  解压完成" -ForegroundColor Green

# 恢复用户配置
if ($backupConfig -and (Test-Path $backupConfig)) {
    Copy-Item -Path $backupConfig\* -Destination "$INSTALL_DIR\config\" -Recurse -Force
    Remove-Item -Path $backupConfig -Recurse -Force
    Write-Host "  已保留用户配置" -ForegroundColor Green
}

# ── 4. 添加到 PATH ──
$binDir = "$INSTALL_DIR\bin\Release"
$currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($currentPath -notlike "*$binDir*") {
    [Environment]::SetEnvironmentVariable("Path", "$currentPath;$binDir", "User")
    # 刷新当前会话的 PATH
    $env:Path = "$env:Path;$binDir"
    Write-Host "  已添加到用户 PATH" -ForegroundColor Green
} else {
    Write-Host "  PATH 已存在，跳过" -ForegroundColor Gray
}

# ── 5. 写入卸载脚本到安装目录 ──
$uninstallScript = @'
# CLFCode 卸载脚本
Write-Host "● CLFCode 卸载" -ForegroundColor Cyan
$installDir = "$env:USERPROFILE\CLFCode"
$binDir = "$installDir\bin\Release"

# 从 PATH 移除
$currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($currentPath -like "*$binDir*") {
    $newPath = ($currentPath -split ";" | Where-Object { $_ -ne $binDir }) -join ";"
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    Write-Host "  已从 PATH 移除" -ForegroundColor Green
}

# 删除安装目录
if (Test-Path $installDir) {
    Remove-Item -Path $installDir -Recurse -Force
    Write-Host "  已删除: $installDir" -ForegroundColor Green
}
Write-Host "● 卸载完成" -ForegroundColor Cyan
'@
$uninstallScript | Out-File -FilePath "$INSTALL_DIR\uninstall.ps1" -Encoding UTF8

# ── 6. 验证 ──
Write-Host ""
if (Test-Path "$binDir\CLFCode.exe") {
    Write-Host "✔ CLFCode $latestVersion 安装成功!" -ForegroundColor Green
    Write-Host ""
    Write-Host "  使用方法:" -ForegroundColor White
    Write-Host "    1. 打开终端，进入任意项目目录" -ForegroundColor Gray
    Write-Host "    2. 输入 CLFCode 启动" -ForegroundColor Gray
    Write-Host ""
    Write-Host "  卸载:" -ForegroundColor White
    Write-Host "    powershell $INSTALL_DIR\uninstall.ps1" -ForegroundColor Gray
    Write-Host ""
    Write-Host "  升级:" -ForegroundColor White
    Write-Host "    irm https://gitee.com/$REPO_OWNER/$REPO_NAME/raw/master/upgrade.ps1 | iex" -ForegroundColor Gray
} else {
    Write-Host "✗ 安装验证失败，请检查" -ForegroundColor Red
    exit 1
}
