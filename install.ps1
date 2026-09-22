# CLFCode Windows 安装脚本
# 用法: irm https://gitee.com/sherlock0923/CLFCode/raw/master/install.ps1 | iex
#
# 安装到 %USERPROFILE%\CLFCode，自动添加到用户 PATH，无需管理员权限
#
# 2026-09-22 加固（S1-S6，唯一权威实现；upgrade.ps1 已薄壳化调用本脚本 -Upgrade）：
#   S1 前置占用探测（关键文件独占打开试探；命中即退出，零破坏发生）
#   S2 删目录改"改名后删"（同卷 rename——锁文件不锁目录；唯一递归删除
#      推迟到新版本装好之后，对象是新名字的旧目录；失败路径自动回滚）
#   S3 安装 Move 前断言目标不存在（消除嵌套安装潜伏缺陷）
#   S4 备份清理收敛：成功路径删除、失败路径保留 1 份并提示路径
#   S6 卸载模板加固：Stop + 撞锁明确报错退出（不再"半删后假报完成"）

param([switch]$Upgrade)

$ErrorActionPreference = "Stop"

$REPO_OWNER = "sherlock0923"
$REPO_NAME  = "CLFCode"

# 测试钩子（V1-V4 验证用）：未设置时行为与默认完全一致
$INSTALL_DIR = if ($env:CLFCODE_TEST_INSTALL_DIR) { $env:CLFCODE_TEST_INSTALL_DIR }
               else { "$env:USERPROFILE\CLFCode" }
$SKIP_PATH    = [bool]$env:CLFCODE_TEST_SKIP_PATH

# S1：关键文件清单（独占打开探测占用；文件不存在则跳过）
$PROBE_FILES = @("bin\Release\CLFCode.exe", "doc\log\clf_agent.log")
$DATA_DIRS   = @("config", "doc\contextHistory", "doc\log", "doc\debug")
$binDir      = "$INSTALL_DIR\bin\Release"

Write-Host "● CLFCode 安装程序" -ForegroundColor Cyan
Write-Host "  安装目录: $INSTALL_DIR"
Write-Host ""

# ── S1：占用探测（先于一切删除动作；命中即退出，目录零改动） ──
function Test-ClfFileLocked {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    try {
        $fs = [System.IO.File]::Open($Path, 'Open', 'ReadWrite', 'None')
        $fs.Dispose()
        return $false
    } catch [System.IO.IOException] { return $true }
}

function Assert-ClfNotLocked {
    foreach ($rel in $PROBE_FILES) {
        $p = Join-Path $INSTALL_DIR $rel
        if (Test-ClfFileLocked $p) {
            Write-Host "  ✗ 文件被占用: $p" -ForegroundColor Red
            Write-Host "    请先退出正在运行的 CLFCode（或占用该文件的进程）后重试" -ForegroundColor Yellow
            exit 1
        }
    }
}

# ── S2：删目录改"改名后删"（Directory.Move 真 rename，原子切换；失败即退出，
#    绝不半删。注意：Windows 下目录内文件被独占锁时 rename 会被拒绝——
#    这正好把"占用"挡在破坏动作之前，零破坏中止） ──
function Move-ClfOldAside {
    $old = "$INSTALL_DIR.old-$([guid]::NewGuid().ToString('N'))"
    [System.IO.Directory]::Move($INSTALL_DIR, $old)
    return $old
}

function Remove-ClfOldBestEffort {
    param([string]$Old)
    try {
        Remove-Item -Path $Old -Recurse -Force
    } catch {
        Write-Host "  ⚠ 旧版本目录未能删除（仍有文件被占用）: $Old" -ForegroundColor Yellow
        Write-Host "    新版本已装好；确认无误后可手动删除该目录" -ForegroundColor Gray
    }
}

# ── S3：安装 Move（目标不存在断言——不再依赖"上一步删干净了"） ──
function Install-ClfData {
    param([string]$ExtractDir)
    if (Test-Path -LiteralPath $INSTALL_DIR) {
        throw "安装目录已存在（$INSTALL_DIR）——中止，避免嵌套安装"
    }
    $inner = Get-ChildItem -Path $ExtractDir -Directory | Select-Object -First 1
    if ($inner) { Move-Item -Path $inner.FullName -Destination $INSTALL_DIR }
    else        { Move-Item -Path $ExtractDir -Destination $INSTALL_DIR }
}

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

# ── 1.5 检查本地已安装版本 ──
$localVersionPath = "$INSTALL_DIR\VERSION"
$localVersion = ""
$updateAvailable = $true
if (Test-Path -LiteralPath $localVersionPath) {
    $localVersion = (Get-Content $localVersionPath -Raw).Trim()
    if ($localVersion -eq $latestVersion) {
        Write-Host ""
        Write-Host "✔ 已是最新版本 ($localVersion)" -ForegroundColor Green
        Write-Host "  升级: irm https://gitee.com/$REPO_OWNER/$REPO_NAME/raw/master/upgrade.ps1 | iex" -ForegroundColor Gray
        Write-Host "  卸载: & `$env:USERPROFILE\CLFCode\uninstall.ps1" -ForegroundColor Gray
        $updateAvailable = $false
    } else {
        Write-Host "  本地版本: $localVersion → 更新到 $latestVersion" -ForegroundColor Yellow
    }
}
if (-not $updateAvailable) { return }

# 如果本地已有版本但不同，提示升级
if ($localVersion -and $localVersion -ne $latestVersion) {
    Write-Host "  建议使用升级脚本: irm https://gitee.com/$REPO_OWNER/$REPO_NAME/raw/master/upgrade.ps1 | iex" -ForegroundColor Yellow
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

# ── 3. 安装（加固主线：探测 → 备份 → 改名 → 解压装新 → 恢复 → 删旧名 → 失败回滚） ──
Write-Host "  正在安装..." -ForegroundColor Gray
$backupRoot = $null
$oldDir     = $null
try {
    if (Test-Path -LiteralPath $INSTALL_DIR) {
        # S1：占用探测（先于备份与一切删除动作）
        Assert-ClfNotLocked
        # 保留用户数据：配置 / 会话历史 / 日志 / 崩溃转储（覆盖安装不丢数据）
        # GUID 唯一目录名：避免旧固定目录残留导致下次备份嵌套错乱
        $backupRoot = "$env:TEMP\CLFCode_backup_$([guid]::NewGuid().ToString('N'))"
        foreach ($rel in $DATA_DIRS) {
            $srcPath = Join-Path $INSTALL_DIR $rel
            if (Test-Path -LiteralPath $srcPath) {
                $dstPath = Join-Path $backupRoot $rel
                New-Item -ItemType Directory -Path (Split-Path $dstPath) -Force | Out-Null
                Copy-Item -Path $srcPath -Destination $dstPath -Recurse -Force
            }
        }
        Write-Host "  用户数据已备份（配置/会话历史/日志）" -ForegroundColor Gray
        # S2：改名后删（取代裸 Remove-Item——绝不半删在用目录）
        $oldDir = Move-ClfOldAside
    }

    # 解压到临时目录，再移动到目标位置（zip 内顶层是 CLFCode-vX.Y.Z/）
    $tempExtract = "$env:TEMP\CLFCode_extract"
    if (Test-Path -LiteralPath $tempExtract) { Remove-Item -Path $tempExtract -Recurse -Force }
    Expand-Archive -Path $zipPath -DestinationPath $tempExtract -Force
    Remove-Item -Path $zipPath -Force

    # S3：安装 Move（目标不存在断言，防嵌套）
    Install-ClfData $tempExtract
    if (Test-Path -LiteralPath $tempExtract) { Remove-Item -Path $tempExtract -Recurse -Force }
    Write-Host "  解压完成" -ForegroundColor Green

    # 恢复用户数据（配置 / 会话历史 / 日志 / 崩溃转储）
    if ($backupRoot -and (Test-Path -LiteralPath $backupRoot)) {
        foreach ($rel in $DATA_DIRS) {
            $srcPath = Join-Path $backupRoot $rel
            if (Test-Path -LiteralPath $srcPath) {
                $dstPath = Join-Path $INSTALL_DIR $rel
                New-Item -ItemType Directory -Path $dstPath -Force | Out-Null
                # 管道形式：空目录时无条目，不会触发通配符无匹配报错
                Get-ChildItem -Path $srcPath -Force | Copy-Item -Destination $dstPath -Recurse -Force
            }
        }
        # S4：备份清理在成功路径（失败路径保留救命副本，见 catch）
        Remove-Item -Path $backupRoot -Recurse -Force
        Write-Host "  已恢复用户数据（配置/会话历史/日志）" -ForegroundColor Green
    }

    # S2 尾段：新版本装好后才尽力删旧名
    if ($oldDir) { Remove-ClfOldBestEffort $oldDir }
} catch {
    # 回滚：装新未完成 → 旧目录改回原名（绝不留下半删残骸）
    if ($oldDir -and (Test-Path -LiteralPath $oldDir) -and -not (Test-Path -LiteralPath $INSTALL_DIR)) {
        Move-Item -Path $oldDir -Destination $INSTALL_DIR -ErrorAction SilentlyContinue
        Write-Host "  已回滚：原安装目录保持原样" -ForegroundColor Yellow
    }
    if ($backupRoot -and (Test-Path -LiteralPath $backupRoot)) {
        Write-Host "  备份保留于: $backupRoot（排查/恢复用，可手动删除）" -ForegroundColor Yellow
    }
    Write-Host "  ✗ 安装失败: $_" -ForegroundColor Red
    Write-Host "    提示：请勿在安装目录内启动终端后升级；若文件被占用请先关闭相关进程" -ForegroundColor Yellow
    exit 1
}

# ── 4. 添加到 PATH ──
if (-not $SKIP_PATH) {
    $currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
    if ($currentPath -notlike "*$binDir*") {
        [Environment]::SetEnvironmentVariable("Path", "$currentPath;$binDir", "User")
        # 刷新当前会话的 PATH
        $env:Path = "$env:Path;$binDir"
        Write-Host "  已添加到用户 PATH" -ForegroundColor Green
    } else {
        Write-Host "  PATH 已存在，跳过" -ForegroundColor Gray
    }
}

# ── 5. 写入卸载脚本到安装目录（S6：Stop + 撞锁明确报错退出） ──
$uninstallScript = @'
# CLFCode 卸载脚本
$ErrorActionPreference = "Stop"
Write-Host "● CLFCode 卸载" -ForegroundColor Cyan
$installDir = if ($env:CLFCODE_TEST_INSTALL_DIR) { $env:CLFCODE_TEST_INSTALL_DIR }
              else { "$env:USERPROFILE\CLFCode" }
$binDir = "$installDir\bin\Release"

# 从 PATH 移除
$currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($currentPath -like "*$binDir*") {
    $newPath = ($currentPath -split ";" | Where-Object { $_ -ne $binDir }) -join ";"
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    Write-Host "  已从 PATH 移除" -ForegroundColor Green
}

# 删除安装目录（改名后删：Directory.Move 原子 rename——文件被占用时
# rename 会被 Windows 拒绝，报错退出、零破坏，绝不半删）
if (Test-Path -LiteralPath $installDir) {
    $oldDir = "$installDir.old-$([guid]::NewGuid().ToString('N'))"
    try {
        [System.IO.Directory]::Move($installDir, $oldDir)
        Write-Host "  已移除: $installDir" -ForegroundColor Green
        try {
            Remove-Item -Path $oldDir -Recurse -Force
            Write-Host "  已清理全部文件" -ForegroundColor Green
        } catch {
            Write-Host "  ⚠ 部分文件被占用（如 doc\log\clf_agent.log），残余保留于: $oldDir" -ForegroundColor Yellow
            Write-Host "    关闭占用进程后，可手动删除该目录" -ForegroundColor Gray
        }
    } catch {
        Write-Host "  ✗ 卸载失败：安装目录中有文件被占用（请先退出正在运行的 CLFCode）" -ForegroundColor Red
        Write-Host "    目录保持原样: $installDir" -ForegroundColor Yellow
        exit 1
    }
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
