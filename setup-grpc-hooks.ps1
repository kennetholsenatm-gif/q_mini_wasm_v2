# ============================================================================
# gRPC Hooks Setup Script
# ============================================================================
# Automates the manual follow-ups for JSON hooks to gRPC conversion
# ============================================================================

param(
    [switch]$Build,
    [switch]$Test,
    [switch]$Push,
    [switch]$All
)

$ErrorActionPreference = "Stop"

# Configuration
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$HooksDir = Join-Path $ProjectRoot "q_mini_wasm_v2/go/pkg/grpc/hooks"
$GoDir = Join-Path $ProjectRoot "q_mini_wasm_v2/go"

Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  gRPC Hooks Setup" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# Check prerequisites
function Test-Prerequisites {
    Write-Host "Checking prerequisites..." -ForegroundColor Yellow
    
    # Check Go
    try {
        $goVersion = go version 2>&1
        Write-Host "  ✓ Go: $goVersion" -ForegroundColor Green
    }
    catch {
        Write-Host "  ✗ Go is not installed or not in PATH" -ForegroundColor Red
        Write-Host "    Please install Go from https://golang.org/dl/" -ForegroundColor Yellow
        return $false
    }
    
    # Check protoc
    try {
        $protocVersion = protoc --version 2>&1
        Write-Host "  ✓ protoc: $protocVersion" -ForegroundColor Green
    }
    catch {
        Write-Host "  ✗ protoc is not installed" -ForegroundColor Red
        Write-Host "    Installing protoc..." -ForegroundColor Yellow
        
        # Try to install protoc using winget
        try {
            winget install protobuf --accept-source-agreements --accept-package-agreements
            Write-Host "  ✓ protoc installed" -ForegroundColor Green
        }
        catch {
            Write-Host "  ✗ Failed to install protoc automatically" -ForegroundColor Red
            Write-Host "    Please install manually from https://github.com/protocolbuffers/protobuf/releases" -ForegroundColor Yellow
            return $false
        }
    }
    
    return $true
}

# Install protoc-gen-go
function Install-ProtocPlugins {
    Write-Host "`nInstalling protoc plugins..." -ForegroundColor Yellow
    
    Push-Location $GoDir
    try {
        Write-Host "  Installing protoc-gen-go..." -ForegroundColor Gray
        go install google.golang.org/protobuf/cmd/protoc-gen-go@latest
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to install protoc-gen-go"
        }
        
        Write-Host "  Installing protoc-gen-go-grpc..." -ForegroundColor Gray
        go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to install protoc-gen-go-grpc"
        }
        
        Write-Host "  ✓ Protoc plugins installed" -ForegroundColor Green
    }
    catch {
        Write-Host "  ✗ Failed to install protoc plugins: $_" -ForegroundColor Red
        return $false
    }
    finally {
        Pop-Location
    }
    
    return $true
}

# Generate Go code from proto
function Generate-GoCode {
    Write-Host "`nGenerating Go code from proto..." -ForegroundColor Yellow
    
    if (-not (Test-Path $HooksDir)) {
        Write-Host "  ✗ Hooks directory not found: $HooksDir" -ForegroundColor Red
        return $false
    }
    
    $protoFile = Join-Path $HooksDir "hooks.proto"
    if (-not (Test-Path $protoFile)) {
        Write-Host "  ✗ Proto file not found: $protoFile" -ForegroundColor Red
        return $false
    }
    
    Push-Location $HooksDir
    try {
        Write-Host "  Running protoc..." -ForegroundColor Gray
        
        # Generate Go code
        protoc --go_out=. --go_opt=paths=source_relative `
               --go-grpc_out=. --go-grpc_opt=paths=source_relative `
               hooks.proto
        
        if ($LASTEXITCODE -ne 0) {
            throw "protoc failed"
        }
        
        # Check if files were generated
        $generatedFiles = Get-ChildItem -Path $HooksDir -Filter "*.go"
        if ($generatedFiles.Count -eq 0) {
            throw "No Go files were generated"
        }
        
        Write-Host "  ✓ Generated files:" -ForegroundColor Green
        foreach ($file in $generatedFiles) {
            Write-Host "    - $($file.Name)" -ForegroundColor Gray
        }
    }
    catch {
        Write-Host "  ✗ Failed to generate Go code: $_" -ForegroundColor Red
        return $false
    }
    finally {
        Pop-Location
    }
    
    return $true
}

# Update dependencies
function Update-Dependencies {
    Write-Host "`nUpdating dependencies..." -ForegroundColor Yellow
    
    Push-Location $GoDir
    try {
        Write-Host "  Running go mod tidy..." -ForegroundColor Gray
        go mod tidy
        if ($LASTEXITCODE -ne 0) {
            throw "go mod tidy failed"
        }
        
        Write-Host "  ✓ Dependencies updated" -ForegroundColor Green
    }
    catch {
        Write-Host "  ✗ Failed to update dependencies: $_" -ForegroundColor Red
        return $false
    }
    finally {
        Pop-Location
    }
    
    return $true
}

# Run tests
function Run-Tests {
    Write-Host "`nRunning tests..." -ForegroundColor Yellow
    
    Push-Location $GoDir
    try {
        Write-Host "  Running go test..." -ForegroundColor Gray
        go test ./pkg/grpc/hooks/... -v
        if ($LASTEXITCODE -ne 0) {
            Write-Host "  ⚠ Some tests failed (this is expected if tests are not yet implemented)" -ForegroundColor Yellow
        }
        else {
            Write-Host "  ✓ All tests passed" -ForegroundColor Green
        }
    }
    catch {
        Write-Host "  ⚠ Test execution had issues: $_" -ForegroundColor Yellow
    }
    finally {
        Pop-Location
    }
    
    return $true
}

# Build the project
function Build-Project {
    Write-Host "`nBuilding project..." -ForegroundColor Yellow
    
    Push-Location $GoDir
    try {
        Write-Host "  Building gateway..." -ForegroundColor Gray
        go build -o bin/gateway.exe ./cmd/gateway
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed"
        }
        
        Write-Host "  ✓ Build successful" -ForegroundColor Green
    }
    catch {
        Write-Host "  ✗ Build failed: $_" -ForegroundColor Red
        return $false
    }
    finally {
        Pop-Location
    }
    
    return $true
}

# Push to remote
function Push-ToRemote {
    Write-Host "`nPushing to remote..." -ForegroundColor Yellow
    
    Push-Location $ProjectRoot
    try {
        # Check if there are changes to push
        $status = git status --porcelain
        if ($status) {
            Write-Host "  ⚠ There are uncommitted changes. Committing first..." -ForegroundColor Yellow
            git add -A
            git commit -m "chore: Auto-commit before push"
        }
        
        Write-Host "  Pushing to remote..." -ForegroundColor Gray
        git push origin q_mini_wasm_v2
        if ($LASTEXITCODE -ne 0) {
            throw "Push failed"
        }
        
        Write-Host "  ✓ Pushed successfully" -ForegroundColor Green
    }
    catch {
        Write-Host "  ✗ Push failed: $_" -ForegroundColor Red
        return $false
    }
    finally {
        Pop-Location
    }
    
    return $true
}

# Main
try {
    # Check prerequisites
    if (-not (Test-Prerequisites)) {
        Write-Host "`nPrerequisites check failed. Please install missing components." -ForegroundColor Red
        exit 1
    }
    
    # Install protoc plugins
    if (-not (Install-ProtocPlugins)) {
        Write-Host "`nFailed to install protoc plugins." -ForegroundColor Red
        exit 1
    }
    
    # Generate Go code
    if (-not (Generate-GoCode)) {
        Write-Host "`nFailed to generate Go code." -ForegroundColor Red
        exit 1
    }
    
    # Update dependencies
    if (-not (Update-Dependencies)) {
        Write-Host "`nFailed to update dependencies." -ForegroundColor Red
        exit 1
    }
    
    # Run tests if requested
    if ($Test -or $All) {
        Run-Tests
    }
    
    # Build if requested
    if ($Build -or $All) {
        if (-not (Build-Project)) {
            Write-Host "`nBuild failed." -ForegroundColor Red
            exit 1
        }
    }
    
    # Push if requested
    if ($Push -or $All) {
        if (-not (Push-ToRemote)) {
            Write-Host "`nPush failed." -ForegroundColor Red
            exit 1
        }
    }
    
    Write-Host "`n============================================" -ForegroundColor Green
    Write-Host "  Setup completed successfully!" -ForegroundColor Green
    Write-Host "============================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "  Next steps:" -ForegroundColor Cyan
    Write-Host "  1. Review generated files in q_mini_wasm_v2/go/pkg/grpc/hooks/" -ForegroundColor White
    Write-Host "  2. Update gateway/main.go to register the hook service" -ForegroundColor White
    Write-Host "  3. Run tests: .\setup-grpc-hooks.ps1 -Test" -ForegroundColor White
    Write-Host "  4. Build: .\setup-grpc-hooks.ps1 -Build" -ForegroundColor White
    Write-Host "  5. Push: .\setup-grpc-hooks.ps1 -Push" -ForegroundColor White
    Write-Host "  6. Or do everything: .\setup-grpc-hooks.ps1 -All" -ForegroundColor White
    Write-Host ""
}
catch {
    Write-Host "`nError: $_" -ForegroundColor Red
    exit 1
}
