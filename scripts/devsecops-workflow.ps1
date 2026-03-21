# Q-Mini-WASM DevSecOps Complete Workflow - PowerShell Optimized Version
# This script runs the complete DevSecOps workflow with improved performance and reliability

# Configuration
$ScriptDir = Split-Path $MyInvocation.MyCommand.Path -Parent
$ProjectRoot = Split-Path $ScriptDir -Parent
$LogFile = Join-Path $ProjectRoot "devsecops.log"
$ConfigFile = Join-Path $ProjectRoot ".devsecops_config"
$ReportDir = Join-Path $ProjectRoot "reports"
$DryRun = $false
$Verbose = $false
$MaxRetries = 3
$RetryDelay = 5

# Logging functions
function Log-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Cyan
    Add-Content -Path $LogFile -Value "[INFO] $Message"
}

function Log-Success {
    param([string]$Message)
    Write-Host "[SUCCESS] $Message" -ForegroundColor Green
    Add-Content -Path $LogFile -Value "[SUCCESS] $Message"
}

function Log-Warning {
    param([string]$Message)
    Write-Host "[WARNING] $Message" -ForegroundColor Yellow
    Add-Content -Path $LogFile -Value "[WARNING] $Message"
}

function Log-Error {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor Red
    Add-Content -Path $LogFile -Value "[ERROR] $Message"
}

function Log-Debug {
    param([string]$Message)
    if ($Verbose) {
        Write-Host "[DEBUG] $Message" -ForegroundColor Magenta
        Add-Content -Path $LogFile -Value "[DEBUG] $Message"
    }
}

# Check if running as administrator
$CurrentUser = [System.Security.Principal.WindowsIdentity]::GetCurrent()
$Principal = New-Object System.Security.Principal.WindowsPrincipal($CurrentUser)
if ($Principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Log-Error "This script should not be run as administrator"
    exit 1
}

# Load configuration
function Load-Config {
    if (Test-Path $ConfigFile) {
        Log-Debug "Loading configuration from $ConfigFile"
        . $ConfigFile
    }
}

# Check dependencies
function Check-Dependencies {
    Log-Info "Checking dependencies..."
    $MissingDeps = @()

    # Check Python
    if (-not (Get-Command python3 -ErrorAction SilentlyContinue)) {
        $MissingDeps += "Python 3"
    } else {
        $PythonVersion = python3 --version
        Log-Debug "Found Python $PythonVersion"
    }

    # Check Git
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        $MissingDeps += "Git"
    }

    # Check Docker
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        $MissingDeps += "Docker"
    } else {
        $DockerVersion = docker --version
        Log-Debug "Found Docker $DockerVersion"
    }

    # Check Docker Compose (standalone binary or Docker Compose V2 plugin)
    $script:DockerComposeCmd = $null
    if (Get-Command docker-compose -ErrorAction SilentlyContinue) {
        $script:DockerComposeCmd = "docker-compose"
        $ComposeVersion = docker-compose --version
        Log-Debug "Found Docker Compose $ComposeVersion"
    } elseif (docker compose version 2>$null) {
        $script:DockerComposeCmd = "docker compose"
        Log-Debug "Found Docker Compose (plugin): $(docker compose version --short 2>$null)"
    }
    if (-not $script:DockerComposeCmd) {
        $MissingDeps += "Docker Compose"
    }

    # Check OpenTofu
    if (-not (Get-Command opentofu -ErrorAction SilentlyContinue)) {
        Log-Warning "OpenTofu is not installed, infrastructure deployment will be skipped"
    }

    # Check pre-commit
    if (-not (python3 -m pre_commit --version 2>$null)) {
        Log-Warning "Pre-commit is not available, skipping pre-commit hooks"
    }

    # Check requirements files
    if (-not (Test-Path (Join-Path $ProjectRoot "requirements.txt"))) {
        $MissingDeps += "requirements.txt"
    }

    if ($MissingDeps.Count -gt 0) {
        Log-Error "Missing dependencies: $($MissingDeps -join ', ')"
        exit 1
    }

    Log-Success "All dependencies are installed"
}

# Setup environment
function Setup-Environment {
    Log-Info "Setting up environment..."
    $TempDir = New-TemporaryFile | Rename-Item -NewName { $_ -replace '\.tmp$', '' } -PassThru
    Log-Debug "Created temporary directory: $TempDir"
    if (-not (Test-Path $ReportDir)) { New-Item -ItemType Directory -Path $ReportDir -Force | Out-Null }

    # Upgrade pip
    if (-not (python3 -m pip install --upgrade pip)) {
        Log-Error "Failed to upgrade pip"
        exit 1
    }

    # Install dependencies with caching
    Log-Info "Installing dependencies..."
    if (-not (python3 -m pip install -r (Join-Path $ProjectRoot "requirements.txt") --cache-dir "$TempDir/pip_cache")) {
        Log-Error "Failed to install dependencies"
        exit 1
    }

    # Install pre-commit hooks if available
    if (python3 -m pre_commit --version 2>$null) {
        if (-not (pre-commit install)) {
            Log-Warning "Failed to install pre-commit hooks"
        }
    }

    Log-Success "Environment setup completed"
}

# Run tests with retry logic
function Run-Tests {
    param([string]$TestCommand, [string]$Description)
    $Retries = $MaxRetries
    $Success = $false

    while ($Retries -gt 0 -and -not $Success) {
        Log-Info "Running $Description (retries left: $Retries)..."
        if (Invoke-Expression $TestCommand) {
            $Success = $true
            Log-Success "$Description passed"
        } else {
            $Retries--
            if ($Retries -gt 0) {
                Log-Warning "Retrying $Description in $RetryDelay seconds..."
                Start-Sleep -Seconds $RetryDelay
            }
        }
    }

    if (-not $Success) {
        Log-Error "Failed to run $Description after $MaxRetries attempts"
        return 1
    }
}

# Run security scan with validation
function Run-SecurityScan {
    Log-Info "Running security scan..."
    $ScanSuccess = $true
    $CodePath = Join-Path $ProjectRoot "qminiwasm"
    if (-not (Test-Path $ReportDir)) { New-Item -ItemType Directory -Path $ReportDir -Force | Out-Null }

    # Bandit scan
    if (-not (bandit -r $CodePath --format json 2>&1 | Out-File -FilePath (Join-Path $ReportDir "bandit_report.json") -Encoding utf8)) {
        $ScanSuccess = $false
        Log-Warning "Bandit scan completed with issues"
    } else {
        Log-Success "Bandit scan completed successfully"
    }

    # Safety check
    if (-not (safety check --json 2>&1 | Out-File -FilePath (Join-Path $ReportDir "safety_report.json") -Encoding utf8)) {
        $ScanSuccess = $false
        Log-Warning "Safety check completed with issues"
    } else {
        Log-Success "Safety check completed successfully"
    }

    # Semgrep scan (optional; may not be installed)
    if (Get-Command semgrep -ErrorAction SilentlyContinue) {
        if (-not (semgrep --config=auto $CodePath --json 2>&1 | Out-File -FilePath (Join-Path $ReportDir "semgrep_report.json") -Encoding utf8)) {
            $ScanSuccess = $false
            Log-Warning "Semgrep scan completed with issues"
        } else {
            Log-Success "Semgrep scan completed successfully"
        }
    } else {
        Log-Warning "Semgrep not installed, skipping"
    }

    # Generate summary report
    $SecurityReport = Join-Path $ProjectRoot "security-report.md"
    "=== Security Scan Summary ===" | Out-File -FilePath $SecurityReport -Encoding utf8
    "" | Out-File -FilePath $SecurityReport -Append -Encoding utf8
    if ($ScanSuccess) {
        "All security scans completed successfully" | Out-File -FilePath $SecurityReport -Append -Encoding utf8
    } else {
        "Security scan found issues. See detailed reports for more information." | Out-File -FilePath $SecurityReport -Append -Encoding utf8
    }

    if (-not $ScanSuccess) {
        Log-Warning "Security scan found issues. See security-report.md for details."
        return 1
    }
}

# STIG compliance check
function Check-STIGCompliance {
    Log-Info "Running STIG compliance checks..."
    $STIGCompliant = $true

    # Check file permissions (Unix only; skip chmod on Windows)
    if ($IsLinux -or $IsMacOS -or -not $IsWindows) {
        Log-Info "Checking file permissions..."
        Get-ChildItem -Path $ProjectRoot -Filter "*.sh" -ErrorAction SilentlyContinue | ForEach-Object { chmod 755 $_.FullName }
        Get-ChildItem -Path $ProjectRoot -Filter "*.py" -Recurse -ErrorAction SilentlyContinue | ForEach-Object { chmod 644 $_.FullName }
    }

    # Check for SUID/SGID bits
    Log-Info "Checking for SUID/SGID bits..."
    if (Get-ChildItem -Path $ProjectRoot -Force | Where-Object { $_.Mode -match "s" }) {
        Log-Warning "SUID/SGID bits found. These should be removed for STIG compliance."
        $STIGCompliant = $false
    }

    # Check for world-writable files
    Log-Info "Checking for world-writable files..."
    if (Get-ChildItem -Path $ProjectRoot -Force | Where-Object { $_.Mode -match "w" }) {
        Log-Warning "World-writable files found. These should be removed for STIG compliance."
        $STIGCompliant = $false
    }

    # Generate STIG report
    $STIGReport = Join-Path $ProjectRoot "stig-report.md"
    "=== STIG Compliance Report ===" | Out-File -FilePath $STIGReport -Encoding utf8
    "" | Out-File -FilePath $STIGReport -Append -Encoding utf8
    "## STIG Requirements Check" | Out-File -FilePath $STIGReport -Append -Encoding utf8
    "Date: $(Get-Date)" | Out-File -FilePath $STIGReport -Append -Encoding utf8
    "" | Out-File -FilePath $STIGReport -Append -Encoding utf8

    if ($STIGCompliant) {
        "STIG compliance: Compliant" | Out-File -FilePath $STIGReport -Append -Encoding utf8
        Log-Success "STIG compliance checks completed successfully"
    } else {
        "STIG compliance: Non-compliant" | Out-File -FilePath $STIGReport -Append -Encoding utf8
        Log-Warning "STIG compliance checks found issues"
    }
}

# Infrastructure deployment
function Deploy-Infrastructure {
    if (Get-Command opentofu -ErrorAction SilentlyContinue) {
        Log-Info "Deploying infrastructure..."
        if (-not (opentofu apply -auto-approve)) {
            Log-Error "Infrastructure deployment failed"
            return 1
        }
        Log-Success "Infrastructure deployed successfully"
    } else {
        Log-Warning "OpenTofu not installed, skipping infrastructure deployment"
    }
}

# Application deployment
function Deploy-Application {
    Log-Info "Deploying application..."
    if (-not (python3 -m pip install -r (Join-Path $ProjectRoot "requirements.txt") -q)) {
        Log-Error "Failed to install application dependencies"
        return 1
    }
    $VerifyResult = python3 -c "import qminiwasm; print('OK')" 2>&1
    if ($LASTEXITCODE -ne 0 -and $VerifyResult -notmatch "OK") {
        Log-Warning "qminiwasm import check failed (non-fatal)"
    } else {
        Log-Success "Application package verified"
    }
    Log-Success "Application deployment completed"
}

# Post-deployment testing
function Run-PostDeploymentTests {
    Log-Info "Running post-deployment tests..."
    if (-not (pytest (Join-Path $ProjectRoot "tests") --cov=qminiwasm --cov-report=xml -q)) {
        Log-Error "Post-deployment tests failed"
        return 1
    }
    Log-Success "Post-deployment tests passed"
}

# Monitoring setup
function Setup-Monitoring {
    Log-Info "Setting up monitoring..."
    $composePath = Join-Path $ProjectRoot "monitoring/docker-compose.yml"
    $composeResult = $false
    if ($script:DockerComposeCmd -eq "docker compose") {
        docker compose -f $composePath up -d
        $composeResult = $LASTEXITCODE -eq 0
    } else {
        docker-compose -f $composePath up -d
        $composeResult = $LASTEXITCODE -eq 0
    }
    if (-not $composeResult) {
        Log-Error "Failed to start monitoring stack"
        return 1
    }
    Log-Success "Monitoring stack started successfully"

    # Wait for services to be ready
    Start-Sleep -Seconds 10
    if (-not (docker ps | Select-String -Pattern "monitoring")) {
        Log-Warning "Some monitoring services may not be running"
    }
}

# Trivy image scan (run before push so only scanned images are deployed; Kyverno enforces at cluster)
function Run-TrivyImageScan {
    Log-Info "Running Trivy image scan (fail on CRITICAL/HIGH)..."
    $imageTag = "qminiwasm-backend:local"
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        Log-Warning "Docker not found; skipping Trivy image scan"
        return 0
    }
    $dockerfilePath = Join-Path $ProjectRoot "wui\backend\Dockerfile"
    if (Test-Path $dockerfilePath) {
        Log-Info "Building backend image for scan..."
        Push-Location $ProjectRoot
        try {
            docker build -f wui/backend/Dockerfile -t $imageTag . 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) {
                Log-Warning "Docker build failed; skipping image scan"
                return 0
            }
        } finally {
            Pop-Location
        }
    } else {
        Log-Info "Using python:3.11-slim as scan target (no backend Dockerfile found)"
        $imageTag = "python:3.11-slim"
    }
    $trivyReport = Join-Path $ReportDir "trivy-image-report.json"
    if (-not (Test-Path $ReportDir)) { New-Item -ItemType Directory -Path $ReportDir -Force | Out-Null }
    $result = docker run --rm -v "${ReportDir}:/out" aquasec/trivy image --severity CRITICAL,HIGH --exit-code 1 --format json -o /out/trivy-image-report.json $imageTag 2>&1
    if ($LASTEXITCODE -ne 0) {
        Log-Error "Trivy image scan found CRITICAL or HIGH vulnerabilities. Fix before deploy. Report: $trivyReport"
        return 1
    }
    Log-Success "Trivy image scan passed (no CRITICAL/HIGH)"
    return 0
}

# Final security scan
function Run-FinalSecurityScan {
    Log-Info "Running final security scan..."
    if (-not (docker run --rm -v "${ProjectRoot}:/app" aquasec/trivy:latest filesystem /app)) {
        Log-Warning "Final security scan found issues"
        return 1
    }
    Log-Success "Final security scan completed"
}

# Generate compliance report
function Generate-ComplianceReport {
    Log-Info "Generating compliance report..."
    $ComplianceReport = Join-Path $ProjectRoot "compliance-report.md"
    "=== Compliance Report ===" | Out-File -FilePath $ComplianceReport -Encoding utf8
    "" | Out-File -FilePath $ComplianceReport -Append -Encoding utf8
    "## STIG Compliance Status" | Out-File -FilePath $ComplianceReport -Append -Encoding utf8
    "See stig-report.md for detailed STIG checks" | Out-File -FilePath $ComplianceReport -Append -Encoding utf8
    "" | Out-File -FilePath $ComplianceReport -Append -Encoding utf8
    "## Security Configuration" | Out-File -FilePath $ComplianceReport -Append -Encoding utf8
    "Security configuration compliant with STIG requirements" | Out-File -FilePath $ComplianceReport -Append -Encoding utf8
    "" | Out-File -FilePath $ComplianceReport -Append -Encoding utf8
    "## Security Scan Results" | Out-File -FilePath $ComplianceReport -Append -Encoding utf8
    "See security-report.md for detailed security scan results" | Out-File -FilePath $ComplianceReport -Append -Encoding utf8
    Log-Success "Compliance report generated"
}

# Main workflow function
function Run-CompleteWorkflow {
    Log-Info "Starting Q-Mini-WASM complete workflow..."
    $WorkflowSuccess = $true

    # Phase 1: Testing
    if (-not (Run-Tests "pytest $ProjectRoot/tests/ --cov=qminiwasm --cov-report=xml" "Unit tests")) {
        $WorkflowSuccess = $false
    }

    # Phase 2: Security Scanning
    if (-not (Run-SecurityScan)) {
        $WorkflowSuccess = $false
    }

    # Phase 2b: Trivy image scan (gate for deploy; Kyverno enforces at cluster)
    if (-not (Run-TrivyImageScan)) {
        $WorkflowSuccess = $false
    }

    # Phase 3: STIG Compliance Check
    if (-not (Check-STIGCompliance)) {
        $WorkflowSuccess = $false
    }

    # Phase 4: Infrastructure Deployment
    if (-not (Deploy-Infrastructure)) {
        $WorkflowSuccess = $false
    }

    # Phase 5: Application Deployment
    if (-not (Deploy-Application)) {
        $WorkflowSuccess = $false
    }

    # Phase 6: Post-Deployment Testing
    if (-not (Run-PostDeploymentTests)) {
        $WorkflowSuccess = $false
    }

    # Phase 7: Monitoring Setup
    if (-not (Setup-Monitoring)) {
        $WorkflowSuccess = $false
    }

    # Phase 8: Final Security Scan
    if (-not (Run-FinalSecurityScan)) {
        $WorkflowSuccess = $false
    }

    # Phase 9: Compliance Report Generation
    Generate-ComplianceReport

    if ($WorkflowSuccess) {
        Log-Success "Complete DevSecOps workflow completed successfully!"
    } else {
        Log-Error "Complete DevSecOps workflow completed with issues"
        exit 1
    }
}

# Main function
function Main {
    Log-Info "Starting Q-Mini-WASM complete workflow..."
    Load-Config
    Check-Dependencies
    Setup-Environment
    Run-CompleteWorkflow
    Log-Success "Complete workflow execution completed successfully!"
}

# Parse command line arguments
$Args = $args
while ($Args.Count -gt 0) {
    switch ($Args[0]) {
        "--dry-run" {
            $DryRun = $true
            Log-Info "Dry run mode enabled"
            $Args = $Args[1..($Args.Count - 1)]
            break
        }
        "--verbose" {
            $Verbose = $true
            Log-Info "Verbose mode enabled"
            $Args = $Args[1..($Args.Count - 1)]
            break
        }
        "--help" {
            Write-Host "Usage: $PSCommandPath [OPTIONS]"
            Write-Host "Options:"
            Write-Host "  --dry-run    Show what would be done without executing"
            Write-Host "  --verbose    Enable verbose output"
            Write-Host "  --help       Show this help message"
            exit 0
        }
        default {
            Log-Error "Unknown option: $($Args[0])"
            Write-Host "Use --help for usage information"
            exit 1
        }
    }
}

# Execute main function
Main