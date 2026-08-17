# CLFCode Release Script
# Usage: powershell -File release.ps1 [--no-upload] [--tag vX.Y.Z]
# Tokens: $env:GITEE_TOKEN / $env:GITHUB_TOKEN

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $ScriptDir

$NoUpload = $args -contains "--no-upload"
$TagOverride = $null
for ($i = 0; $i -lt $args.Count; $i++) {
    if ($args[$i] -eq "--tag" -and ($i + 1) -lt $args.Count) { $TagOverride = $args[$i + 1] }
}

# Read version
if ($TagOverride) {
    $Version = $TagOverride.TrimStart('v')
    $Tag = "v$Version"
} else {
    $Raw = Get-Content "$ScriptDir\VERSION" -Raw
    $Tag = $Raw.Trim()
    $Version = $Tag.TrimStart('v')
}
$ZipName = "CLFCode-$Tag-win64.zip"
$ReleaseDir = "$ScriptDir\release\CLFCode-$Tag"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  CLFCode $Tag Release" -ForegroundColor White
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# ========================================
# 1. Build
# ========================================
Write-Host "[1/5] Building Release..." -ForegroundColor Cyan

# 构建目录：cmake-build-release（CLion 默认命名；旧脚本指向不存在的 build_rel/build，
# 曾导致构建失败后静默打包陈旧 exe——发布必崩事故的间接成因）
$BuildDir = "cmake-build-release"
if (-not (Test-Path "$ScriptDir\$BuildDir\build.ninja")) {
    Write-Host "ERROR: $BuildDir not configured." -ForegroundColor Red
    Write-Host "  Run: cmake -S . -B $BuildDir -DCMAKE_BUILD_TYPE=Release -G Ninja" -ForegroundColor Yellow
    exit 1
}

# MSVC 环境导入（普通 PowerShell 缺 INCLUDE/LIB；从 vcvars64 的 cmd 输出回填）
$vcvars = "D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if ((Test-Path $vcvars) -and -not $env:VCToolsInstallDir) {
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
}

# 删除旧 exe，防止"构建失败但 exe 残留"时静默打包陈旧产物
$exe = "$ScriptDir\bin\Release\CLFCode.exe"
Remove-Item $exe -ErrorAction SilentlyContinue

cmake --build "$ScriptDir\$BuildDir" --target CLFCode --config Release -j6 2>&1 | Select-Object -Last 5
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exe)) {
    Write-Host "ERROR: Build failed (exit $LASTEXITCODE)" -ForegroundColor Red
    exit 1
}

# 新旧自检：exe 不得早于最新源码（防御增量构建异常）
$exeTime = (Get-Item $exe).LastWriteTime
$newestSrc = Get-ChildItem "$ScriptDir\src" -Recurse -Include *.cpp,*.hpp |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($exeTime -lt $newestSrc.LastWriteTime) {
    Write-Host "ERROR: exe ($exeTime) older than $($newestSrc.Name) ($($newestSrc.LastWriteTime))" -ForegroundColor Red
    exit 1
}
Write-Host "  Done" -ForegroundColor Green

# ========================================
# 2. Package
# ========================================
Write-Host "[2/5] Packaging $ZipName ..." -ForegroundColor Cyan

# Clean and create dirs
if (Test-Path $ReleaseDir) { Remove-Item $ReleaseDir -Recurse -Force }
New-Item -ItemType Directory -Path "$ReleaseDir\bin\Release" -Force | Out-Null

# Copy exe
Copy-Item "$ScriptDir\bin\Release\CLFCode.exe" "$ReleaseDir\bin\Release\" -Force

# Copy runtime DLLs from previous release (exclude current tag)
# MSVC 构建仅依赖 OpenSSL 动态库对；旧脚本全量复制 *.dll 会把历史 MinGW
# 运行库（libgcc_s_seh/libstdc++-6 等）逐版携带——只取实际导入的 DLL
$PrevDir = Get-ChildItem "$ScriptDir\release" -Directory |
    Where-Object { $_.Name -match '^CLFCode-v' -and $_.Name -ne "CLFCode-$Tag" } |
    Sort-Object Name -Descending | Select-Object -First 1
if ($PrevDir) {
    foreach ($dll in @('libssl-4-x64.dll', 'libcrypto-4-x64.dll')) {
        $src = "$($PrevDir.FullName)\bin\Release\$dll"
        if (Test-Path $src) { Copy-Item $src "$ReleaseDir\bin\Release\" -Force }
    }
    Write-Host "  DLLs: $((Get-ChildItem "$ReleaseDir\bin\Release\*.dll").Count) files (OpenSSL pair)" -ForegroundColor Gray
    # Config, data, doc
    foreach ($sub in @('config', 'data', 'doc')) {
        $src = "$($PrevDir.FullName)\$sub"
        if (Test-Path $src) { Copy-Item $src "$ReleaseDir\" -Recurse -Force }
    }
}

# VERSION + readme
Copy-Item "$ScriptDir\VERSION" "$ReleaseDir\" -Force
@"
CLFCode $Tag -- CLI Agent Framework for Code

Install / Upgrade / Uninstall:

  irm https://gitee.com/sherlock0923/CLFCode/raw/master/install.ps1 | iex
  irm https://gitee.com/sherlock0923/CLFCode/raw/master/upgrade.ps1 | iex
  & "$env:USERPROFILE\CLFCode\uninstall.ps1"

Edit %USERPROFILE%\CLFCode\config\agent_settings.json before first run.
Manual install: extract to %USERPROFILE%\CLFCode\

Gitee : https://gitee.com/sherlock0923/CLFCode
GitHub: https://github.com/wjhwhs888/CLFCode
"@ | Out-File -FilePath "$ReleaseDir\README.txt" -Encoding UTF8

# Generate zip
$ZipPath = "$ScriptDir\release\$ZipName"
if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
Compress-Archive -Path $ReleaseDir -DestinationPath $ZipPath -Force
$ZipSize = [math]::Round((Get-Item $ZipPath).Length / 1MB, 1)
Write-Host "  Zip: $ZipSize MB" -ForegroundColor Green

# ========================================
# 3. Release notes from CHANGELOG
# ========================================
Write-Host "[3/5] Extracting release notes..." -ForegroundColor Cyan
$Changelog = Get-Content "$ScriptDir\CHANGELOG.md" -Raw -Encoding UTF8
$EscapedTag = [regex]::Escape($Tag)
$Pattern = '(?s)## ' + $EscapedTag + '.*?(?=## v\d|$)'
$Match = [regex]::Match($Changelog, $Pattern)
$ReleaseBody = if ($Match.Success) { $Match.Value.Trim() } else { "CLFCode $Tag" }
Write-Host "  Done" -ForegroundColor Green

if ($NoUpload) {
    Write-Host ""
    Write-Host "=== Package ready (upload skipped) ===" -ForegroundColor Yellow
    Write-Host "  $ZipPath" -ForegroundColor White
    exit 0
}

# ========================================
# 4. Gitee
# ========================================
Write-Host "[4/5] Uploading to Gitee..." -ForegroundColor Cyan
$GT = $env:GITEE_TOKEN
if (-not $GT) {
    Write-Host "  GITEE_TOKEN not set, skipped" -ForegroundColor Yellow
} else {
    try {
        # 删除同 tag 旧 Release
        $existingUrl = "https://gitee.com/api/v5/repos/sherlock0923/CLFCode/releases/tags/$Tag"
        try {
            $existing = Invoke-RestMethod -Uri "$existingUrl?access_token=$GT" -Method Get -TimeoutSec 10
            if ($existing.id) {
                Invoke-RestMethod -Uri "https://gitee.com/api/v5/repos/sherlock0923/CLFCode/releases/$($existing.id)?access_token=$GT" -Method Delete -TimeoutSec 10 | Out-Null
                Write-Host "  Deleted old release id=$($existing.id)" -ForegroundColor Gray
            }
        } catch { Write-Host "  No old release to delete" -ForegroundColor Gray }

        $Body = @{ access_token = $GT; tag_name = $Tag; name = $Tag;
                   body = $ReleaseBody; target_commitish = "master" } | ConvertTo-Json
        $BodyBytes = [System.Text.Encoding]::UTF8.GetBytes($Body)
        $Rel = Invoke-RestMethod -Uri "https://gitee.com/api/v5/repos/sherlock0923/CLFCode/releases" `
            -Method Post -Body $BodyBytes -ContentType "application/json; charset=utf-8" -TimeoutSec 30
        Write-Host "  Release created" -ForegroundColor Green
        $uploadUrl = "https://gitee.com/api/v5/repos/sherlock0923/CLFCode/releases/$($Rel.id)/attach_files?access_token=$GT"
        $curlCmd = "curl.exe -s -X POST -H accept:application/json ""$uploadUrl"" -F ""file=@$ZipPath"""
        cmd.exe /c $curlCmd 2>&1 | Out-Null
        Write-Host "  Attachment uploaded ($ZipSize MB)" -ForegroundColor Green
    } catch { Write-Host "  FAILED: $_" -ForegroundColor Red }
}

# ========================================
# 5. GitHub
# ========================================
Write-Host "[5/5] Uploading to GitHub..." -ForegroundColor Cyan
$GH = $env:GITHUB_TOKEN
if (-not $GH) {
    Write-Host "  GITHUB_TOKEN not set, skipped" -ForegroundColor Yellow
} else {
    try {
        $Headers = @{ Authorization = "Bearer $GH"; Accept = "application/vnd.github+json" }
        $Body = @{ tag_name = $Tag; name = $Tag; body = $ReleaseBody } | ConvertTo-Json
        $BodyBytes = [System.Text.Encoding]::UTF8.GetBytes($Body)
        $Rel = Invoke-RestMethod -Uri "https://api.github.com/repos/wjhwhs888/CLFCode/releases" `
            -Method Post -Body $BodyBytes -Headers $Headers -ContentType "application/json; charset=utf-8" -TimeoutSec 30
        $UploadUrl = $Rel.upload_url -replace '\{.*\}', "?name=$ZipName"
        Invoke-RestMethod -Uri $UploadUrl -Method Post `
            -Headers @{ Authorization = "Bearer $GH"; "Content-Type" = "application/zip" } `
            -InFile $ZipPath -TimeoutSec 120 | Out-Null
        Write-Host "  Done" -ForegroundColor Green
    } catch { Write-Host "  FAILED: $_" -ForegroundColor Red }
}

Write-Host ""
Write-Host "=== Release $Tag complete ===" -ForegroundColor Green
Write-Host "  Zip: $ZipName ($ZipSize MB)"
