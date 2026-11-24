param(
    [string]$DeviceUrl = "http://192.168.178.123",
    [string]$AccessToken,
    [string]$CredentialsPath = "$env:USERPROFILE\.claude\.credentials.json"
)

$ErrorActionPreference = "Stop"
$DeviceUrl = $DeviceUrl.TrimEnd("/")
$ClientId = "9d1c250a-e61b-44d9-88ed-5944d1962f5e"

Write-Host "Claude OAuth helper for ESP Usage"
Write-Host "Browser sessionKey cookies get HTTP 403 from Cloudflare outside Chrome/Edge."
Write-Host "This script pushes a Claude Code OAuth access token (sk-ant-oat...) instead."
Write-Host ""

function Save-ClaudeCredentials($cred, $path) {
    $json = $cred | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText($path, $json)
}

function Get-RefreshedAccessToken([string]$path) {
    if (-not (Test-Path $path)) {
        throw "Claude Code credentials not found at $path. Install/login Claude Code once, then re-run."
    }
    $cred = Get-Content $path -Raw | ConvertFrom-Json
    if (-not $cred.claudeAiOauth -or -not $cred.claudeAiOauth.refreshToken) {
        throw "No claudeAiOauth.refreshToken in $path. Run Claude Code login first."
    }

    $nowMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $expiresAt = [int64]$cred.claudeAiOauth.expiresAt
    $access = [string]$cred.claudeAiOauth.accessToken
    if ($access -and $expiresAt -gt ($nowMs + 120000)) {
        Write-Host "Using existing Claude Code access token (still valid)."
        return $access
    }

    Write-Host "Refreshing Claude Code OAuth token..."
    $body = @{
        grant_type    = "refresh_token"
        refresh_token = [string]$cred.claudeAiOauth.refreshToken
        client_id     = $ClientId
    } | ConvertTo-Json -Compress

    try {
        $tok = Invoke-RestMethod -Method Post -Uri "https://platform.claude.com/v1/oauth/token" `
            -ContentType "application/json" -Body $body `
            -Headers @{ "anthropic-beta" = "oauth-2025-04-20"; "User-Agent" = "claude-code/2.0.0" }
    } catch {
        $detail = $_.Exception.Message
        if ($_.ErrorDetails.Message) { $detail = $_.ErrorDetails.Message }
        throw @"
Token refresh failed: $detail

Fix: open Claude Code (or run 'claude'), complete Google login there, then re-run:
  $($MyInvocation.MyCommand.Path) -DeviceUrl $DeviceUrl
"@
    }

    $cred.claudeAiOauth.accessToken = [string]$tok.access_token
    if ($tok.refresh_token) { $cred.claudeAiOauth.refreshToken = [string]$tok.refresh_token }
    $cred.claudeAiOauth.expiresAt = [int64]($nowMs + ([int]$tok.expires_in * 1000))
    Save-ClaudeCredentials $cred $path
    Write-Host "Refresh OK; credentials file updated."
    return [string]$tok.access_token
}

if ([string]::IsNullOrWhiteSpace($AccessToken)) {
    $AccessToken = Get-RefreshedAccessToken $CredentialsPath
} else {
    $AccessToken = $AccessToken.Trim()
    if ($AccessToken.StartsWith("Bearer ")) { $AccessToken = $AccessToken.Substring(7).Trim() }
}

if ($AccessToken.StartsWith("sk-ant-sid")) {
    throw "sessionKey cookies cannot be used (Cloudflare 403). Use Claude Code OAuth (sk-ant-oat...)."
}
if (-not $AccessToken.StartsWith("sk-ant-oat")) {
    Write-Host "Warning: token does not start with sk-ant-oat; pushing anyway."
}

Write-Host "Verifying usage endpoint..."
$usage = Invoke-RestMethod -Method Get -Uri "https://api.anthropic.com/api/oauth/usage" -Headers @{
    Authorization     = "Bearer $AccessToken"
    "anthropic-beta"  = "oauth-2025-04-20"
    "User-Agent"      = "claude-code/2.0.0"
    Accept            = "application/json"
}
Write-Host ("Usage OK: 5h={0}% weekly={1}%" -f $usage.five_hour.utilization, $usage.seven_day.utilization)

Write-Host "Pushing OAuth token to $DeviceUrl ..."
$result = Invoke-RestMethod -Method Post -Uri "$DeviceUrl/api/claude/session" -Body @{
    session_key = $AccessToken
    oauth_token = $AccessToken
} -ContentType "application/x-www-form-urlencoded"
$result | ConvertTo-Json -Compress
Write-Host "Done. Device will refresh Claude usage shortly."
