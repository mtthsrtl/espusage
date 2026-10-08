param(
    [string]$DeviceUrl = "http://192.168.178.123",
    [string]$Email,
    [string]$Password,
    [ValidateSet("global", "cn")]
    [string]$Region = "global",
    [string]$Serial,
    [string]$VerifyCode
)

$ErrorActionPreference = "Stop"
$DeviceUrl = $DeviceUrl.TrimEnd("/")
$apiBase = if ($Region -eq "cn") { "https://api.bambulab.cn" } else { "https://api.bambulab.com" }

function Get-JwtUserId([string]$jwt) {
    $parts = $jwt.Split(".")
    if ($parts.Length -lt 2) { return $null }
    $payload = $parts[1].Replace("-", "+").Replace("_", "/")
    while ($payload.Length % 4) { $payload += "=" }
    $json = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($payload)) | ConvertFrom-Json
    if ($json.username -and $json.username -like "u_*") { return $json.username.Substring(2) }
    if ($json.user_id) { return [string]$json.user_id }
    return $null
}

$headers = @{
    "User-Agent"            = "bambu_network_agent/01.09.05.01"
    "X-BBL-Client-Name"     = "OrcaSlicer"
    "X-BBL-Client-Type"     = "slicer"
    "X-BBL-Client-Version"  = "01.09.05.51"
    "X-BBL-Language"        = "en-US"
    "X-BBL-OS-Type"         = "linux"
    "X-BBL-Agent-Version"   = "01.09.05.01"
    "accept"                = "application/json"
    "Content-Type"          = "application/json"
}

if ([string]::IsNullOrWhiteSpace($Email)) {
    $Email = Read-Host "Bambu account email"
}
if ([string]::IsNullOrWhiteSpace($Password) -and [string]::IsNullOrWhiteSpace($VerifyCode)) {
    $secure = Read-Host "Bambu password" -AsSecureString
    $Password = [System.Net.NetworkCredential]::new("", $secure).Password
}

$body = @{ account = $Email; apiError = "" }
if ($VerifyCode) { $body.code = $VerifyCode } else { $body.password = $Password }

Write-Host "Logging in at $apiBase ..."
try {
    $login = Invoke-RestMethod -Method Post -Uri "$apiBase/v1/user-service/user/login" -Headers $headers -Body ($body | ConvertTo-Json -Compress)
} catch {
    throw "Bambu login request failed: $($_.Exception.Message)"
}

if (-not $login.accessToken) {
    if ($login.loginType -eq "verifyCode") {
        Write-Host "Bambu requests an email verification code. Requesting code..."
        Invoke-RestMethod -Method Post -Uri "$apiBase/v1/user-service/user/sendemail/code" -Headers $headers -Body (@{ email = $Email; type = "codeLogin" } | ConvertTo-Json -Compress) | Out-Null
        $code = Read-Host "Enter the code from your email"
        & $PSCommandPath -DeviceUrl $DeviceUrl -Email $Email -Region $Region -Serial $Serial -VerifyCode $code
        return
    }
    throw "Login failed: $($login | ConvertTo-Json -Compress)"
}

$token = [string]$login.accessToken
$userId = Get-JwtUserId $token
if (-not $userId) {
    $pref = Invoke-RestMethod -Method Get -Uri "$apiBase/v1/design-user-service/my/preference" -Headers (@{ Authorization = "Bearer $token" } + $headers)
    $userId = [string]$pref.uid
}
if (-not $userId) { throw "Could not determine Bambu user id." }

if ([string]::IsNullOrWhiteSpace($Serial)) {
    $devices = Invoke-RestMethod -Method Get -Uri "$apiBase/v1/iot-service/api/user/bind" -Headers (@{ Authorization = "Bearer $token" } + $headers)
    $list = @($devices.devices)
    if ($list.Count -eq 0) { throw "No printers bound to this Bambu account." }
    $a1 = $list | Where-Object { $_.dev_product_name -match "A1" -or $_.dev_model_name -match "A1|N2S" } | Select-Object -First 1
    $Serial = if ($a1) { $a1.dev_id } else { $list[0].dev_id }
    Write-Host "Using printer serial: $Serial"
}

$form = @{
    bambu_on     = "1"
    bambu_mode   = "cloud"
    bambu_region = $Region
    bambu_acct   = $Email
    bambu_user   = $userId
    bambu_token  = $token
    bambu_serial = $Serial
}

Write-Host "Pushing cloud session to $DeviceUrl ..."
$push = Invoke-RestMethod -Method Post -Uri "$DeviceUrl/api/bambu/session" -Body $form
$push | ConvertTo-Json -Compress
Write-Host "Done. Open: $DeviceUrl/api/view?screen=print"
