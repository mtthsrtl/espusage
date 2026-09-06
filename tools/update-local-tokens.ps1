param(
    [string]$DeviceUrl = "http://192.168.178.123",
    [Parameter(Mandatory = $true)]
    [string]$ApiKey
)

$ErrorActionPreference = "Stop"
$DeviceUrl = $DeviceUrl.TrimEnd("/")

$codexAuthPath = Join-Path $env:USERPROFILE ".codex\auth.json"
if (-not (Test-Path -LiteralPath $codexAuthPath)) {
    throw "Codex auth file not found: $codexAuthPath"
}

$codexAuth = Get-Content -LiteralPath $codexAuthPath -Raw | ConvertFrom-Json
$codexToken = [string]$codexAuth.tokens.access_token
$codexAccountId = [string]$codexAuth.tokens.account_id
if ([string]::IsNullOrWhiteSpace($codexToken)) {
    throw "No Codex access token found in $codexAuthPath"
}

$cursorDbPath = Join-Path $env:APPDATA "Cursor\User\globalStorage\state.vscdb"
if (-not (Test-Path -LiteralPath $cursorDbPath)) {
    throw "Cursor state database not found: $cursorDbPath"
}

$cursorReader = 'import sqlite3,sys; row=sqlite3.connect(sys.argv[1]).execute("SELECT value FROM ItemTable WHERE key=''cursorAuth/accessToken''").fetchone(); print(row[0] if row else "")'
$cursorToken = (& python -c $cursorReader $cursorDbPath).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($cursorToken)) {
    throw "No Cursor access token could be read. Make sure Python and Cursor are installed."
}

$body = @{
    api_key          = $ApiKey
    codex_token      = $codexToken
    codex_account_id = $codexAccountId
    cursor_token     = $cursorToken
}

$result = Invoke-RestMethod -Uri "$DeviceUrl/api/tokens" -Method Post -ContentType "application/x-www-form-urlencoded" -Body $body
if (-not $result.ok) {
    throw "The device rejected the token update."
}

Write-Host "Tokens updated and usage refresh queued on $DeviceUrl"
