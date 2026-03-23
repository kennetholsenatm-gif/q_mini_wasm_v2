# Greenfield deployment orchestration for Q-Mini-WASM.
# Runs security stack -> data stack; documents WUI deploy with env checks.
# Usage: .\scripts\greenfield-deploy.ps1 [-Yes] [-PackerValidate] [-SkipWui]
# Run from repo root.

param(
    [switch]$Yes,
    [switch]$PackerValidate,
    [switch]$SkipWui
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $RepoRoot

function Prompt-Step {
    param([string]$Message)
    if ($Yes) { return $true }
    $r = Read-Host "$Message [y/N]"
    return ($r -match '^[yY]')
}

function Test-Env {
    param([string]$Dir, [string]$Example)
    $envPath = Join-Path $Dir ".env"
    if (-not (Test-Path $envPath)) {
        Write-Error "Missing $envPath. Copy from $Example and set secrets."
    }
}

# Optional: validate Packer (no build)
if ($PackerValidate) {
    $PackerDir = Join-Path $RepoRoot "infra\image-builder\packer"
    if (-not (Test-Path $PackerDir)) {
        Write-Error "Packer dir not found: $PackerDir"
    }
    Write-Host "Validating Packer (no build)..."
    Push-Location $PackerDir
    try {
        packer init .
        packer validate .
    } finally { Pop-Location }
    Write-Host "Packer validation OK."
}

# Step 2: Security stack
Write-Host "--- Step 2: Security stack ---"
$SecurityDir = Join-Path $RepoRoot "containers\security-stack"
Test-Env $SecurityDir ".env.example"
if (-not (Prompt-Step "Start security stack (Keycloak, Vault, Envoy)?")) { exit 0 }
Push-Location $SecurityDir
try { docker compose up -d } finally { Pop-Location }
Write-Host "Security stack started. Keycloak :8080, Vault :8200, Envoy :8081."

# Step 3: Data stack
Write-Host "--- Step 3: Data stack ---"
$DataDir = Join-Path $RepoRoot "containers\data-stack"
Test-Env $DataDir ".env.example"
if (-not (Prompt-Step "Start data stack (PostgreSQL, RabbitMQ, NiFi, Solace)?")) { exit 0 }
Push-Location $DataDir
try { docker compose up -d } finally { Pop-Location }
Write-Host "Data stack started. See containers/data-stack/README.md for ports."

# Step 4: WUI
if (-not $SkipWui) {
    Write-Host "--- Step 4: WUI ---"
    Write-Host "WUI is not started by this script. To run the backend with Data Stack, set:"
    Write-Host "  POSTGRES_HOST, POSTGRES_PORT, POSTGRES_USER, POSTGRES_PASSWORD, POSTGRES_DB"
    Write-Host "  RABBITMQ_HOST, RABBITMQ_PORT, RABBITMQ_DEFAULT_USER, RABBITMQ_DEFAULT_PASS"
    Write-Host "See docs/Greenfield-Deployment.md and containers/wui/README.md for WUI <-> Data Stack env vars."
} else {
    Write-Host "--- Step 4: WUI (skipped) ---"
}

Write-Host "Greenfield sequence complete."
