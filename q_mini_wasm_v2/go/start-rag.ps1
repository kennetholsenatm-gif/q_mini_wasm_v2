# ============================================================================
# QMINIWASM RAG Service Startup Script
# ============================================================================
# Starts Qdrant vector database and Go gateway with RAG service
# Port: 8088 (REST API), 9090 (gRPC)
# ============================================================================

param(
    [switch]$Build,
    [switch]$NoDocker
)

$ErrorActionPreference = "Stop"

# Configuration
$GatewayPort = 8088
$GrpcPort = 9090
$QdrantRestPort = 6333
$QdrantGrpcPort = 6334
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  QMINIWASM RAG Service Startup" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# Check Docker
function Test-Docker {
    try {
        docker info 2>&1 | Out-Null
        return $true
    }
    catch {
        return $false
    }
}

# Start Qdrant
function Start-Qdrant {
    Write-Host "[1/3] Starting Qdrant vector database..." -ForegroundColor Yellow
    
    # Check if container already running
    $running = docker ps -q -f name=qminiwasm-qdrant 2>$null
    if ($running) {
        Write-Host "  Qdrant already running" -ForegroundColor Green
        return
    }
    
    # Check if image exists locally, pull if not
    $imageExists = docker images -q qdrant/qdrant:latest 2>$null
    if (-not $imageExists) {
        Write-Host "  Pulling Qdrant image (this may take a moment)..." -ForegroundColor Cyan
        docker pull qdrant/qdrant:latest
        if ($LASTEXITCODE -ne 0) {
            Write-Host "  Failed to pull qdrant/qdrant:latest, trying specific version..." -ForegroundColor Yellow
            docker pull qdrant/qdrant:v1.7.4
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to pull Qdrant image"
            }
        }
    }
    
    # Check if container exists but stopped
    $exists = docker ps -aq -f name=qminiwasm-qdrant 2>$null
    if ($exists) {
        Write-Host "  Starting existing Qdrant container..." -ForegroundColor Gray
        docker start qminiwasm-qdrant | Out-Null
    }
    else {
        Write-Host "  Creating new Qdrant container..." -ForegroundColor Gray
        docker run -d `
            --name qminiwasm-qdrant `
            -p "${QdrantRestPort}:6333" `
            -p "${QdrantGrpcPort}:6334" `
            -v qminiwasm-qdrant-data:/qdrant/storage `
            qdrant/qdrant:latest | Out-Null
    }
    
    # Wait for Qdrant to be ready
    Write-Host "  Waiting for Qdrant to be ready..." -ForegroundColor Gray
    $maxAttempts = 30
    $attempt = 0
    while ($attempt -lt $maxAttempts) {
        try {
            $response = Invoke-WebRequest -Uri "http://localhost:${QdrantRestPort}/healthz" -UseBasicParsing -TimeoutSec 2 2>$null
            if ($response.StatusCode -eq 200) {
                Write-Host "  Qdrant is ready!" -ForegroundColor Green
                return
            }
        }
        catch {
            Start-Sleep -Milliseconds 500
            $attempt++
        }
    }
    
    Write-Host "  Warning: Qdrant may not be fully ready" -ForegroundColor Yellow
}

# Build Gateway
function Build-Gateway {
    Write-Host "[2/3] Building Go gateway..." -ForegroundColor Yellow
    
    Push-Location $PSScriptRoot
    try {
        # Clean any cached problematic modules
        Write-Host "  Cleaning module cache..." -ForegroundColor Gray
        go clean -modcache 2>&1 | Out-Null
        
        # Download dependencies using go get (more reliable)
        Write-Host "  Downloading dependencies..." -ForegroundColor Gray
        go get google.golang.org/grpc@v1.80.0 2>&1 | Out-Null
        go get google.golang.org/protobuf@v1.36.11 2>&1 | Out-Null
        
        # Update go.sum
        Write-Host "  Updating go.sum..." -ForegroundColor Gray
        go mod tidy 2>&1 | Out-Null
        
        # Build
        Write-Host "  Building binary..." -ForegroundColor Gray
        $env:CGO_ENABLED = "0"
        go build -o bin/gateway.exe ./cmd/gateway
        
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed"
        }
        
        Write-Host "  Build successful!" -ForegroundColor Green
    }
    finally {
        Pop-Location
    }
}

# Start Gateway
function Start-Gateway {
    Write-Host "[3/3] Starting Go gateway..." -ForegroundColor Yellow
    
    $gatewayExe = Join-Path $PSScriptRoot "bin/gateway.exe"
    
    if (-not (Test-Path $gatewayExe)) {
        Write-Host "  Gateway binary not found. Building..." -ForegroundColor Yellow
        Build-Gateway
    }
    
    Write-Host "  Starting gateway on port ${GatewayPort}..." -ForegroundColor Gray
    Write-Host ""
    Write-Host "============================================" -ForegroundColor Green
    Write-Host "  RAG Service Started Successfully!" -ForegroundColor Green
    Write-Host "============================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "  REST API:  http://localhost:${GatewayPort}" -ForegroundColor Cyan
    Write-Host "  gRPC:      localhost:${GrpcPort}" -ForegroundColor Cyan
    Write-Host "  Qdrant:    http://localhost:${QdrantRestPort}" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "  Endpoints:" -ForegroundColor Gray
    Write-Host "    POST /api/v1/rag/retrieve  - Retrieve context" -ForegroundColor Gray
    Write-Host "    GET  /api/v1/rag/metrics   - Get metrics" -ForegroundColor Gray
    Write-Host "    POST /api/v1/rag/index     - Index document" -ForegroundColor Gray
    Write-Host ""
    Write-Host "  Press Ctrl+C to stop" -ForegroundColor Yellow
    Write-Host "============================================" -ForegroundColor Green
    Write-Host ""
    
    # Start gateway
    & $gatewayExe
}

# Main
try {
    # Check Docker (unless NoDocker flag)
    if (-not $NoDocker) {
        if (-not (Test-Docker)) {
            Write-Host "Error: Docker is not running or not installed" -ForegroundColor Red
            Write-Host "Please start Docker Desktop or use -NoDocker flag" -ForegroundColor Yellow
            exit 1
        }
        
        Start-Qdrant
    }
    else {
        Write-Host "[1/3] Skipping Docker (NoDocker flag)" -ForegroundColor Gray
        Write-Host "  Make sure Qdrant is running on localhost:${QdrantGrpcPort}" -ForegroundColor Yellow
    }
    
    # Build if requested or needed
    $gatewayExe = Join-Path $PSScriptRoot "bin/gateway.exe"
    if ($Build -or (-not (Test-Path $gatewayExe))) {
        Build-Gateway
    }
    else {
        Write-Host "[2/3] Using existing gateway binary" -ForegroundColor Gray
    }
    
    # Start gateway
    Start-Gateway
}
catch {
    Write-Host ""
    Write-Host "Error: $_" -ForegroundColor Red
    exit 1
}