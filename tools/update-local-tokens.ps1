param(
    [string]$DeviceUrl = "http://192.168.178.123",
    [ValidateSet("Codex", "Cursor", "Both")]
    [string]$Provider,
    [string]$ApiKey
)

# Optional: enter the same key configured as "Token webhook API key" in the web UI.
# Keep this empty in commits if the repository is public.
$TokenWebhookApiKey = ""

$ErrorActionPreference = "Stop"
$DeviceUrl = $DeviceUrl.TrimEnd("/")
$body = @{}

if ([string]::IsNullOrWhiteSpace($Provider)) {
    Write-Host "Select credentials to update:"
    Write-Host "  1) Codex"
    Write-Host "  2) Cursor"
    Write-Host "  3) Both"
    $Provider = switch (Read-Host "Selection [1-3]") {
        "1" { "Codex" }
        "2" { "Cursor" }
        "3" { "Both" }
        default { throw "Invalid selection. Choose 1, 2, or 3." }
    }
}

if ([string]::IsNullOrWhiteSpace($ApiKey)) {
    $ApiKey = $TokenWebhookApiKey
}
if ([string]::IsNullOrWhiteSpace($ApiKey)) {
    $ApiKey = $env:ESPUSAGE_TOKEN_API_KEY
}
if ([string]::IsNullOrWhiteSpace($ApiKey)) {
    $secureApiKey = Read-Host "Token webhook API key" -AsSecureString
    $ApiKey = [System.Net.NetworkCredential]::new("", $secureApiKey).Password
}
if ($ApiKey.Length -lt 12) {
    throw "The token webhook API key must contain at least 12 characters."
}
$body.api_key = $ApiKey

if ($Provider -in @("Codex", "Both")) {
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
    $body.codex_token = $codexToken
    if (-not [string]::IsNullOrWhiteSpace($codexAccountId)) {
        $body.codex_account_id = $codexAccountId
    }
}

if ($Provider -in @("Cursor", "Both")) {
    $cursorDbPath = Join-Path $env:APPDATA "Cursor\User\globalStorage\state.vscdb"
    if (-not (Test-Path -LiteralPath $cursorDbPath)) {
        throw "Cursor state database not found: $cursorDbPath"
    }
    # PowerShell mangles nested quotes in `python -c`; use a temp script instead.
    $cursorPy = Join-Path ([System.IO.Path]::GetTempPath()) ("espusage-cursor-token-{0}.py" -f [guid]::NewGuid().ToString("n"))
    $cursorPyBody = @'
import sqlite3, sys
db = sqlite3.connect(sys.argv[1])
row = db.execute("SELECT value FROM ItemTable WHERE key='cursorAuth/accessToken'").fetchone()
print(row[0] if row else "")
'@
    [System.IO.File]::WriteAllText($cursorPy, $cursorPyBody)
    try {
        $cursorOut = & python $cursorPy $cursorDbPath 2>&1
        $cursorToken = ($cursorOut | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($cursorToken) -or $cursorToken -match '(?i)traceback|syntaxerror') {
            throw "No Cursor access token could be read. Make sure Python and Cursor are installed. Details: $cursorToken"
        }
        $body.cursor_token = $cursorToken
    }
    finally {
        Remove-Item -LiteralPath $cursorPy -Force -ErrorAction SilentlyContinue
    }
}

$result = Invoke-RestMethod -Uri "$DeviceUrl/api/tokens" -Method Post -ContentType "application/x-www-form-urlencoded" -Body $body
if (-not $result.ok) {
    throw "The device rejected the token update."
}

Write-Host "$Provider credentials updated and usage refresh queued on $DeviceUrl"
