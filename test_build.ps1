$body = @{ jsonrpc = '2.0'; id = 1; method = 'wui_build_release'; params = @{ output = 'test.exe'; go_binary = 'go'; build_bridge = $true } } | ConvertTo-Json -Compress
try {
    $r = Invoke-RestMethod -Uri 'http://localhost:7345/mcp' -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 10
    Write-Host "SUCCESS: build started"
    $r | ConvertTo-Json -Depth 3
} catch {
    Write-Host "ERROR: $($_.Exception.Message)"
}
