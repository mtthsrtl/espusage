param(
    [string]$DeviceUrl = "http://192.168.178.123",
    [string]$SessionKey,
    [string]$OrgId
)

$ErrorActionPreference = "Stop"
$DeviceUrl = $DeviceUrl.TrimEnd("/")

Write-Host "Claude Google login helper for ESP Usage"
Write-Host "1) Browser opens claude.ai login - complete Google sign-in there."
Write-Host "2) Paste the sessionKey cookie (DevTools > Application > Cookies > sessionKey),"
Write-Host "   or leave blank if you already pushed via the web-UI bookmarklet."
Write-Host ""

Start-Process "https://claude.ai/login" | Out-Null
Read-Host "Press Enter after Google login on claude.ai finished"

if ([string]::IsNullOrWhiteSpace($SessionKey)) {
    $SessionKey = Read-Host "Paste sessionKey cookie value (sk-ant-sid01-...)"
}
$SessionKey = $SessionKey.Trim()
if ($SessionKey.StartsWith("sessionKey=")) { $SessionKey = $SessionKey.Substring(11) }
if ($SessionKey.Contains(";")) { $SessionKey = $SessionKey.Split(";")[0].Trim() }
if ([string]::IsNullOrWhiteSpace($SessionKey)) { throw "sessionKey is required." }

$headers = @{
    "Cookie"  = "sessionKey=$SessionKey"
    "Referer" = "https://claude.ai/settings/usage"
    "Origin"  = "https://claude.ai"
    "Accept"  = "application/json"
}

if ([string]::IsNullOrWhiteSpace($OrgId)) {
    Write-Host "Resolving Claude organization..."
    try {
        $orgs = Invoke-RestMethod -Method Get -Uri "https://claude.ai/api/organizations" -Headers $headers
    } catch {
        throw "Could not list organizations (session may be invalid): $($_.Exception.Message)"
    }
    $list = @()
    if ($orgs -is [System.Array]) { $list = $orgs }
    elseif ($orgs.organizations) { $list = @($orgs.organizations) }
    else { $list = @($orgs) }

    $chat = $list | Where-Object {
        $caps = @($_.capabilities)
        ($caps -contains "chat") -or (($caps | ForEach-Object { "$_" }) -match "claude")
    } | Select-Object -First 1
    if (-not $chat) { $chat = $list | Select-Object -First 1 }
    if (-not $chat -or -not $chat.uuid) { throw "No Claude organization found for this session." }
    $OrgId = [string]$chat.uuid
    Write-Host "Using organization $OrgId"
}

$body = @{
    session_key = $SessionKey
    org_id      = $OrgId
}

Write-Host "Pushing Claude session to $DeviceUrl ..."
$result = Invoke-RestMethod -Method Post -Uri "$DeviceUrl/api/claude/session" -Body $body -ContentType "application/x-www-form-urlencoded"
$result | ConvertTo-Json -Compress
Write-Host "Done. The device will refresh usage shortly."
