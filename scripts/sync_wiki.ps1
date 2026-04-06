#!/usr/bin/env powershell
# Wiki Sync Script for q_mini_wasm_v2
# Run this to push wiki-output to GitHub Wiki

$ErrorActionPreference = "Stop"

$repoRoot = "c:\GitHub\q_mini_wasm_v2"
$wikiOutput = "$repoRoot\wiki-output"
$wikiUrl = "https://github.com/kennetholsenatm-gif/q_mini_wasm_v2.wiki.git"

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "q_mini_wasm_v2 Wiki Sync Tool" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

# Check if wiki-output exists
if (-not (Test-Path $wikiOutput)) {
    Write-Error "wiki-output folder not found at $wikiOutput"
    exit 1
}

$wikiCount = (Get-ChildItem -Path $wikiOutput -Filter "*.md" | Measure-Object).Count
Write-Host "Found $wikiCount wiki pages to sync" -ForegroundColor Green

# Create temporary wiki repo
$tempWiki = "$env:TEMP\q_mini_wasm_v2_wiki"
if (Test-Path $tempWiki) {
    Remove-Item -Path $tempWiki -Recurse -Force
}

Write-Host "`nCloning wiki repository..." -ForegroundColor Yellow
git clone $wikiUrl $tempWiki 2>&1 | Out-Null

if (-not (Test-Path $tempWiki)) {
    # If clone fails (wiki doesn't exist yet), init fresh
    Write-Host "Wiki repo doesn't exist yet, creating fresh..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $tempWiki | Out-Null
    Set-Location $tempWiki
    git init
    git remote add origin $wikiUrl
} else {
    Set-Location $tempWiki
}

# Copy all wiki files
Write-Host "`nCopying wiki pages..." -ForegroundColor Yellow
Copy-Item -Path "$wikiOutput\*" -Destination $tempWiki -Recurse -Force

# Show what will be pushed
Write-Host "`nWiki pages to publish:" -ForegroundColor Cyan
Get-ChildItem -Path $tempWiki -Filter "*.md" | Select-Object -ExpandProperty Name | ForEach-Object {
    Write-Host "  - $_" -ForegroundColor White
}

# Commit and push
Write-Host "`nCommitting changes..." -ForegroundColor Yellow
git add -A
git commit -m "Wiki update: Complete documentation refresh - $(Get-Date -Format 'yyyy-MM-dd')" -m "- Updated Home.md with proper navigation" -m "- Simplified sidebar structure" -m "- Added BettiExtractor trace documentation" -m "- All 38 pages properly formatted"

Write-Host "`nPushing to GitHub Wiki..." -ForegroundColor Yellow
git push -u origin master --force

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n==========================================" -ForegroundColor Green
    Write-Host "SUCCESS! Wiki updated at:" -ForegroundColor Green
    Write-Host "https://github.com/kennetholsenatm-gif/q_mini_wasm_v2/wiki" -ForegroundColor Cyan
    Write-Host "==========================================" -ForegroundColor Green
} else {
    Write-Host "`nPush failed. You may need to:" -ForegroundColor Red
    Write-Host "1. Set up Git credentials" -ForegroundColor Yellow
    Write-Host "2. Or manually copy files from: $wikiOutput" -ForegroundColor Yellow
    Write-Host "3. Paste into: https://github.com/kennetholsenatm-gif/q_mini_wasm_v2/wiki" -ForegroundColor Yellow
}

# Cleanup
Set-Location $repoRoot
Remove-Item -Path $tempWiki -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "`nDone!" -ForegroundColor Green
