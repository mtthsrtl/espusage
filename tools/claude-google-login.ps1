param(
    [string]$DeviceUrl = "http://192.168.178.123",
    [string]$AccessToken,
    [string]$CredentialsPath = "$env:USERPROFILE\.claude\.credentials.json",
    [switch]$ForceLogin
)

$ErrorActionPreference = "Stop"
$DeviceUrl = $DeviceUrl.TrimEnd("/")
$ClientId = "9d1c250a-e61b-44d9-88ed-5944d1962f5e"
$TokenUrl = "https://platform.claude.com/v1/oauth/token"
$AuthUrl = "https://claude.ai/oauth/authorize"
$ManualRedirect = "https://platform.claude.com/oauth/code/callback"
$LocalRedirect = "http://localhost:54545/callback"
$AuthorizeScope = "org:create_api_key user:profile user:inference user:sessions:claude_code user:mcp_servers user:file_upload"

Write-Host "Claude OAuth helper for ESP Usage"
Write-Host "Uses Claude Code OAuth (sk-ant-oat...). Browser sessionKey cookies are blocked by Cloudflare."
Write-Host ""

function Save-ClaudeCredentials($cred, $path) {
    $dir = Split-Path -Parent $path
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
    $json = $cred | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText($path, $json)
}

function New-Base64Url([byte[]]$bytes) {
    [Convert]::ToBase64String($bytes).TrimEnd("=").Replace("+", "-").Replace("/", "_")
}

function New-PkcePair {
    $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    $verifierBytes = New-Object byte[] 32
    $stateBytes = New-Object byte[] 16
    $rng.GetBytes($verifierBytes)
    $rng.GetBytes($stateBytes)
    $verifier = New-Base64Url $verifierBytes
    $state = New-Base64Url $stateBytes
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $challenge = New-Base64Url ($sha.ComputeHash([Text.Encoding]::ASCII.GetBytes($verifier)))
    [pscustomobject]@{ Verifier = $verifier; Challenge = $challenge; State = $state }
}

function Get-AuthorizeUrl([string]$redirectUri, $pkce) {
    $pairs = @(
        "code=true",
        "client_id=$ClientId",
        "response_type=code",
        "redirect_uri=$([uri]::EscapeDataString($redirectUri))",
        "scope=$([uri]::EscapeDataString($AuthorizeScope))",
        "code_challenge=$($pkce.Challenge)",
        "code_challenge_method=S256",
        "state=$($pkce.State)"
    )
    return "$AuthUrl?" + ($pairs -join "&")
}

function Exchange-AuthCode([string]$code, [string]$redirectUri, $pkce) {
    $bodyObj = @{
        grant_type    = "authorization_code"
        code          = $code
        redirect_uri  = $redirectUri
        client_id     = $ClientId
        code_verifier = $pkce.Verifier
        state         = $pkce.State
    }
    $body = $bodyObj | ConvertTo-Json -Compress
    return Invoke-RestMethod -Method Post -Uri $TokenUrl -ContentType "application/json" -Body $body -Headers @{
        "anthropic-beta" = "oauth-2025-04-20"
        "User-Agent"     = "claude-code/2.0.0"
        Accept           = "application/json"
    }
}

function Try-RefreshAccessToken([string]$path) {
    if (-not (Test-Path $path)) { return $null }
    $cred = Get-Content $path -Raw | ConvertFrom-Json
    if (-not $cred.claudeAiOauth -or -not $cred.claudeAiOauth.refreshToken) { return $null }
    $nowMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $expiresAt = [int64]$cred.claudeAiOauth.expiresAt
    $access = [string]$cred.claudeAiOauth.accessToken
    if ($access -and $expiresAt -gt ($nowMs + 120000) -and -not $ForceLogin) {
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
        $tok = Invoke-RestMethod -Method Post -Uri $TokenUrl -ContentType "application/json" -Body $body -Headers @{
            "anthropic-beta" = "oauth-2025-04-20"
            "User-Agent"     = "claude-code/2.0.0"
        }
    } catch {
        Write-Host "Refresh failed; starting interactive Google/Claude login..."
        return $null
    }
    if (-not $cred.claudeAiOauth) { $cred | Add-Member -NotePropertyName claudeAiOauth -NotePropertyValue ([pscustomobject]@{}) -Force }
    $cred.claudeAiOauth.accessToken = [string]$tok.access_token
    if ($tok.refresh_token) { $cred.claudeAiOauth.refreshToken = [string]$tok.refresh_token }
    $cred.claudeAiOauth.expiresAt = [int64]($nowMs + ([int]$tok.expires_in * 1000))
    Save-ClaudeCredentials $cred $path
    Write-Host "Refresh OK."
    return [string]$tok.access_token
}

function Start-InteractiveClaudeLogin([string]$path) {
    $pkce = New-PkcePair
    $redirect = $ManualRedirect
    $url = Get-AuthorizeUrl $redirect $pkce
    Write-Host ""
    Write-Host "Opening browser for Claude/Google login..."
    Write-Host "After login, the page shows an authorization code."
    Write-Host "Paste the FULL code (often looks like CODE#STATE) below."
    Write-Host ""
    Start-Process $url | Out-Null

    $raw = Read-Host "Paste authorization code"
    $raw = $raw.Trim().Trim("'").Trim('"')
    if (-not $raw) { throw "No authorization code provided." }

    $code = $raw
    $returnedState = $null
    if ($raw.Contains("#")) {
        $parts = $raw.Split("#", 2)
        $code = $parts[0].Trim()
        $returnedState = $parts[1].Trim()
    } elseif ($raw.Contains("&")) {
        # Sometimes users paste query strings: code=...&state=...
        foreach ($piece in $raw.TrimStart("?").Split("&")) {
            $kv = $piece.Split("=", 2)
            if ($kv.Count -ne 2) { continue }
            if ($kv[0] -eq "code") { $code = [uri]::UnescapeDataString($kv[1]) }
            if ($kv[0] -eq "state") { $returnedState = [uri]::UnescapeDataString($kv[1]) }
        }
    }
    if ($returnedState -and $returnedState -ne $pkce.State) {
        throw "OAuth state mismatch. Close the browser tab and re-run the helper."
    }

    Write-Host "Exchanging authorization code..."
    $tok = Exchange-AuthCode $code $redirect $pkce
    $nowMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $cred = @{
        claudeAiOauth = @{
            accessToken      = [string]$tok.access_token
            refreshToken     = $(if ($tok.refresh_token) { [string]$tok.refresh_token } else { "" })
            expiresAt        = [int64]($nowMs + ([int]$tok.expires_in * 1000))
            scopes           = @("user:profile", "user:inference", "user:sessions:claude_code", "user:mcp_servers")
            subscriptionType = ""
        }
    }
    Save-ClaudeCredentials ([pscustomobject]$cred) $path
    Write-Host "Login saved to $path"
    return [string]$tok.access_token
}

function Push-TokenToDevice([string]$token, [string]$device) {
    Write-Host "Verifying usage endpoint..."
    $usage = Invoke-RestMethod -Method Get -Uri "https://api.anthropic.com/api/oauth/usage" -Headers @{
        Authorization    = "Bearer $token"
        "anthropic-beta" = "oauth-2025-04-20"
        "User-Agent"     = "claude-code/2.0.0"
        Accept           = "application/json"
    }
    Write-Host ("Usage OK: 5h={0}% weekly={1}%" -f $usage.five_hour.utilization, $usage.seven_day.utilization)

    Write-Host "Pushing OAuth token to $device ..."
    $result = Invoke-RestMethod -Method Post -Uri "$device/api/claude/session" -Body @{
        oauth_token = $token
        session_key = $token
    } -ContentType "application/x-www-form-urlencoded"
    $result | ConvertTo-Json -Compress
    Write-Host "Done. Device will refresh Claude usage shortly."
}

if (-not [string]::IsNullOrWhiteSpace($AccessToken)) {
    $token = $AccessToken.Trim()
    if ($token.StartsWith("Bearer ")) { $token = $token.Substring(7).Trim() }
} else {
    $token = $null
    if (-not $ForceLogin) {
        try { $token = Try-RefreshAccessToken $CredentialsPath } catch { $token = $null }
    }
    if (-not $token) {
        $token = Start-InteractiveClaudeLogin $CredentialsPath
    }
}

if ($token.StartsWith("sk-ant-sid")) {
    throw "sessionKey cookies cannot be used (Cloudflare 403). Re-run without -AccessToken to do OAuth login."
}

Push-TokenToDevice $token $DeviceUrl
