# Creates the Cloudflare tunnel that makes the bridge reachable from any network.
#
# Run this once. It needs a Cloudflare login for the blueflutex.in zone; the
# first run opens a browser page where you pick that zone. After that the
# tunnel is created, DNS is routed, and config.yml is written.

# cloudflared writes its logs to stderr. PowerShell 5.1 turns native stderr
# into a terminating error when this is 'Stop', so keep it at 'Continue' and
# check exit codes explicitly.
$ErrorActionPreference = 'Continue'

$Hostname   = 'chat.blueflutex.in'
$TunnelName = 'esp32-chat'
$BridgePort = 8787
$CloudflaredDir = Join-Path $env:USERPROFILE '.cloudflared'

# --- locate cloudflared -----------------------------------------------------
$Cloudflared = $null
foreach ($candidate in @(
    (Join-Path $PSScriptRoot '..\bin\cloudflared.exe'),
    (Join-Path $env:ProgramFiles 'Cloudflare\Cloudflared\cloudflared.exe'),
    (Join-Path $env:ProgramFiles 'cloudflared\cloudflared.exe')
  )) {
  if (Test-Path $candidate) { $Cloudflared = (Resolve-Path $candidate).Path; break }
}
if (-not $Cloudflared) {
  $cmd = Get-Command cloudflared -ErrorAction SilentlyContinue
  if ($cmd) { $Cloudflared = $cmd.Source }
}
if (-not $Cloudflared) {
  throw 'cloudflared.exe not found. Put it in bin\ or install it with: winget install Cloudflare.cloudflared'
}
Write-Host "cloudflared: $Cloudflared"

# --- 1. login ---------------------------------------------------------------
$CertPath = Join-Path $CloudflaredDir 'cert.pem'
if (-not (Test-Path $CertPath)) {
  Write-Host ''
  Write-Host 'A browser page will open. Sign in to Cloudflare and pick the' -ForegroundColor Yellow
  Write-Host 'blueflutex.in zone, then come back to this window.' -ForegroundColor Yellow
  Write-Host ''
  & $Cloudflared tunnel login
  if ($LASTEXITCODE -ne 0 -or -not (Test-Path $CertPath)) {
    throw 'Login did not complete: cert.pem was not written.'
  }
} else {
  Write-Host "Already logged in ($CertPath)"
}

# --- 2. create the tunnel (idempotent) --------------------------------------
$tunnels = @()
try {
  $raw = & $Cloudflared tunnel list --output json 2>$null
  if ($raw) { $tunnels = @($raw | ConvertFrom-Json) }
} catch { $tunnels = @() }

$tunnel = $tunnels | Where-Object { $_.name -eq $TunnelName } | Select-Object -First 1
if (-not $tunnel) {
  Write-Host "Creating tunnel '$TunnelName'..."
  & $Cloudflared tunnel create $TunnelName
  if ($LASTEXITCODE -ne 0) { throw 'cloudflared tunnel create failed' }
  $raw = & $Cloudflared tunnel list --output json
  $tunnels = @($raw | ConvertFrom-Json)
  $tunnel = $tunnels | Where-Object { $_.name -eq $TunnelName } | Select-Object -First 1
}
if (-not $tunnel) { throw "Tunnel '$TunnelName' was not created." }
$TunnelId = $tunnel.id
Write-Host "Tunnel id: $TunnelId"

# --- 3. point the hostname at the tunnel ------------------------------------
Write-Host "Routing $Hostname -> tunnel..."
& $Cloudflared tunnel route dns $TunnelName $Hostname
# A second run reports "record already exists"; that is fine.
if ($LASTEXITCODE -ne 0) {
  Write-Host '  (route dns reported an error; continuing - it is harmless if the record exists)' -ForegroundColor DarkYellow
}

# --- 4. write config.yml ----------------------------------------------------
New-Item -ItemType Directory -Force -Path $CloudflaredDir | Out-Null
$ConfigPath = Join-Path $CloudflaredDir 'config.yml'
$Credentials = (Join-Path $CloudflaredDir "$TunnelId.json") -replace '\\', '/'
$Yaml = @"
tunnel: $TunnelId
credentials-file: $Credentials

ingress:
  - hostname: $Hostname
    service: http://127.0.0.1:$BridgePort
  - service: http_status:404
"@
Set-Content -Path $ConfigPath -Value $Yaml -Encoding ASCII
Write-Host "Wrote $ConfigPath"

# --- 5. quick check ---------------------------------------------------------
Write-Host ''
Write-Host "Starting the tunnel for a quick check..."
$proc = Start-Process -FilePath $Cloudflared -ArgumentList 'tunnel','--config',$ConfigPath,'run',$TunnelName -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 8

$ok = $false
for ($i = 0; $i -lt 10; $i++) {
  try {
    $r = Invoke-WebRequest "https://$Hostname/health" -UseBasicParsing -TimeoutSec 10
    if ($r.StatusCode -eq 200) { $ok = $true; break }
  } catch { Start-Sleep -Seconds 3 }
}

if ($ok) {
  Write-Host ''
  Write-Host "Public URL is live: https://$Hostname/" -ForegroundColor Green
} else {
  Write-Host ''
  Write-Host "Tunnel started but https://$Hostname/health did not answer yet." -ForegroundColor Yellow
  Write-Host 'DNS can take a minute on first setup. Re-run this script to check again.'
}

if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
Write-Host ''
Write-Host 'Done. start-bridge.cmd now starts this tunnel automatically.'
