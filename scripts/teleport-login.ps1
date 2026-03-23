# Teleport SSO login and Kubernetes context setup for Q-Mini-WASM.
# Usage: Set TELEPORT_PROXY (and optionally TELEPORT_KUBE_CLUSTER), then run:
#   .\scripts\teleport-login.ps1
# No secrets or tokens are logged or echoed.
# Requires: tsh (Teleport client). Install: winget install gravitational.teleport

$ErrorActionPreference = "Stop"

function Write-Info { param([string]$Message) Write-Host "[INFO] $Message" -ForegroundColor Cyan }
function Write-Ok { param([string]$Message) Write-Host "[OK] $Message" -ForegroundColor Green }
function Write-Err { param([string]$Message) Write-Host "[ERROR] $Message" -ForegroundColor Red }

$Proxy = $env:TELEPORT_PROXY
$KubeCluster = $env:TELEPORT_KUBE_CLUSTER

if (-not $Proxy) {
    Write-Err "TELEPORT_PROXY is not set. Set it to your Teleport proxy address (e.g. teleport.example.com)."
    exit 1
}

if (-not (Get-Command tsh -ErrorAction SilentlyContinue)) {
    Write-Err "tsh (Teleport client) is not in PATH. Install it (e.g. winget install gravitational.teleport) and try again."
    exit 1
}

Write-Info "Logging in to Teleport (SSO) at $Proxy ..."
$loginArgs = @("login", "--auth=sso", "--proxy=$Proxy")
$proc = Start-Process -FilePath "tsh" -ArgumentList $loginArgs -NoNewWindow -Wait -PassThru
if ($proc.ExitCode -ne 0) {
    Write-Err "tsh login failed (exit code $($proc.ExitCode))."
    exit $proc.ExitCode
}
Write-Ok "Teleport login succeeded."

if ($KubeCluster) {
    Write-Info "Configuring Kubernetes access for cluster '$KubeCluster' ..."
    $kubeArgs = @("kube", "login", $KubeCluster)
    $proc2 = Start-Process -FilePath "tsh" -ArgumentList $kubeArgs -NoNewWindow -Wait -PassThru
    if ($proc2.ExitCode -ne 0) {
        Write-Err "tsh kube login failed (exit code $($proc2.ExitCode))."
        exit $proc2.ExitCode
    }
    Write-Ok "Kube config updated. Use kubectl as usual; access is gated by Teleport."
} else {
    Write-Info "TELEPORT_KUBE_CLUSTER not set; skipping kube login. Set it to enable kubectl via Teleport."
}

Write-Ok "Done. Use tsh status to see your session; certificates are short-lived."
