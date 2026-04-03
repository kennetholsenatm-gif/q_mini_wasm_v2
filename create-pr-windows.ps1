# PR Creation Script for Windows
Write-Host "=== Pull Request Creation ===" -ForegroundColor Cyan

# Step 1: Commit changes
Write-Host "1. Committing changes..." -ForegroundColor Yellow
git add Home.md _Sidebar.md generate_wiki_simple.py generate-wiki.ps1 run-wiki-generator.bat WIKI_GUIDE.md SOLUTION_SUMMARY.md WINDOWS_ENVIRONMENT.md WINDOWS_EXECUTION_GUIDE.md

git commit -m "docs: Add wiki population files and documentation

- Add Home.md and _Sidebar.md for GitHub Wiki
- Add generate_wiki_simple.py script to generate wiki from docs/
- Add generate-wiki.ps1 PowerShell script for Windows
- Add run-wiki-generator.bat for easy execution
- Add WIKI_GUIDE.md with detailed instructions
- Add SOLUTION_SUMMARY.md explaining the wiki population issue
- Add WINDOWS_ENVIRONMENT.md for Windows environment
- Add WINDOWS_EXECUTION_GUIDE.md for Windows users"

# Step 2: Create branch if detached
Write-Host "`n2. Checking branch..." -ForegroundColor Yellow
$branch = git symbolic-ref --short HEAD 2>$null
if (-not $branch) {
    $branch = "docs/wiki-population-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
    git checkout -b $branch
    Write-Host "Created branch: $branch" -ForegroundColor Green
} else {
    Write-Host "Branch: $branch" -ForegroundColor Cyan
}

# Step 3: Push to origin
Write-Host "`n3. Pushing to origin..." -ForegroundColor Yellow
git push -u origin $branch

# Step 4: Create PR
Write-Host "`n4. Creating pull request..." -ForegroundColor Yellow
$gh = Get-Command gh -ErrorAction SilentlyContinue
if ($gh) {
    $pr = gh pr list --head $branch --base "q_mini_wasm_v2" --json url --jq '.[0].url' 2>$null
    if ($pr) {
        Write-Host "PR exists: $pr" -ForegroundColor Green
    } else {
        $pr = gh pr create --title "docs: Add wiki population files and documentation" --body "Adds files to populate GitHub Wiki with research documentation from docs/research/ directory." --base "q_mini_wasm_v2" --head $branch
        Write-Host "Created PR: $pr" -ForegroundColor Green
    }
} else {
    Write-Host "GitHub CLI not available. Create PR manually:" -ForegroundColor Yellow
    Write-Host "https://github.com/kennetholsenatm-gif/q_mini_wasm_v2/compare" -ForegroundColor Cyan
    Write-Host "Base: q_mini_wasm_v2, Head: $branch" -ForegroundColor White
}

Write-Host "`n=== Summary ===" -ForegroundColor Cyan
Write-Host "PR Title: docs: Add wiki population files and documentation" -ForegroundColor White
Write-Host "Base Branch: q_mini_wasm_v2" -ForegroundColor White
Write-Host "Head Branch: $branch" -ForegroundColor White
Write-Host "Next: Run python generate_wiki_simple.py after merge" -ForegroundColor Yellow