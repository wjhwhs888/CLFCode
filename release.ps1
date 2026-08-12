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
$BuildDir = if (Test-Path "$ScriptDir\build_rel\build.ninja") { "build_rel" } else { "build" }
cmake --build "$ScriptDir\$BuildDir" --target CLFCode --config Release -j6 2>&1 | Select-Object -Last 5
if (-not (Test-Path "$ScriptDir\bin\Release\CLFCode.exe")) {
    Write-Host "ERROR: Build failed" -ForegroundColor Red
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

# Copy DLLs from previous release (exclude current tag)
$PrevDir = Get-ChildItem "$ScriptDir\release" -Directory |
    Where-Object { $_.Name -match '^CLFCode-v' -and $_.Name -ne "CLFCode-$Tag" } |
    Sort-Object Name -Descending | Select-Object -First 1
if ($PrevDir) {
    $DllSrc = "$($PrevDir.FullName)\bin\Release\*.dll"
    foreach ($f in (Get-ChildItem $DllSrc -ErrorAction SilentlyContinue)) {
        Copy-Item $f.FullName "$ReleaseDir\bin\Release\" -Force
    }
    Write-Host "  DLLs: $((Get-ChildItem "$ReleaseDir\bin\Release\*.dll").Count) files" -ForegroundColor Gray
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
        # 删除同 tag 旧 Release（如果存在）
        $existingUrl = "https://gitee.com/api/v5/repos/sherlock0923/CLFCode/releases/tags/$Tag"
        try {
            $existing = Invoke-RestMethod -Uri "$existingUrl?access_token=$GT" -Method Get -TimeoutSec 10
            if ($existing.id) {
                Invoke-RestMethod -Uri "https://gitee.com/api/v5/repos/sherlock0923/CLFCode/releases/$($existing.id)?access_token=$GT" -Method Delete -TimeoutSec 10 | Out-Null
                Write-Host "  Deleted old release (id=$($existing.id))" -ForegroundColor Gray
            }
        } catch {}
        $Body = @{ access_token = $GT; tag_name = $Tag; name = $Tag;
                   body = $ReleaseBody; target_commitish = "master" } | ConvertTo-Json
        $BodyBytes = [System.Text.Encoding]::UTF8.GetBytes($Body)
        $Rel = Invoke-RestMethod -Uri "https://gitee.com/api/v5/repos/sherlock0923/CLFCode/releases" `
            -Method Post -Body $BodyBytes -ContentType "application/json; charset=utf-8" -TimeoutSec 30
        $curlArgs = @(
            "-X", "POST",
            "-H", "accept: application/json",
            "https://gitee.com/api/v5/repos/sherlock0923/CLFCode/releases/$($Rel.id)/attach_files?access_token=$GT",
            "-F", "file=@$ZipPath"
        )
        curl.exe @curlArgs 2>&1 | Out-Null
        Write-Host "  Done" -ForegroundColor Green
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
