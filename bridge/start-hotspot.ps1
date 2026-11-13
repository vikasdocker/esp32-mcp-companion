# Starts (or verifies) the 2.4 GHz hotspot the ESP32 connects to.
# Windows Mobile Hotspot is used because the main Wi-Fi is 5 GHz only.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime

$asTaskAction = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and
    $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction'
  })[0]

function AwaitAction($action) {
  $task = $asTaskAction.Invoke($null, @($action))
  $task.Wait(-1) | Out-Null
}

[Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime] | Out-Null
[Windows.Networking.Connectivity.NetworkInformation, Windows.Networking.Connectivity, ContentType = WindowsRuntime] | Out-Null

$profile = $null
for ($attempt = 0; $attempt -lt 30 -and -not $profile; $attempt++) {
  $profile = [Windows.Networking.Connectivity.NetworkInformation]::GetInternetConnectionProfile()
  if (-not $profile) { Start-Sleep -Seconds 2 }
}
if (-not $profile) {
  throw 'No internet connection profile found after 60 seconds. Connect the PC to Wi-Fi first.'
}

$manager = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager]::CreateFromConnectionProfile($profile)
$config = $manager.GetCurrentAccessPointConfiguration()

$wantSsid = 'ESP32-Chat'
$wantPass = 'blueflutex123'

if ($manager.TetheringOperationalState -eq 'On' -and $config.Ssid -ne $wantSsid) {
  "Hotspot is on as '$($config.Ssid)'; restarting it as '$wantSsid'..."
  AwaitAction $manager.StopTetheringAsync()
  Start-Sleep -Seconds 2
  $config = $manager.GetCurrentAccessPointConfiguration()
}

if ($manager.TetheringOperationalState -eq 'On') {
  "Hotspot already on: $($config.Ssid)"
} else {
  $config.Ssid = $wantSsid
  $config.Passphrase = $wantPass
  $config.Band = [Windows.Networking.NetworkOperators.TetheringWiFiBand]::TwoPointFourGigahertz
  AwaitAction $manager.ConfigureAccessPointAsync($config)
  $operation = $manager.StartTetheringAsync()
  Start-Sleep -Seconds 6
  "Hotspot started: $($config.Ssid) (2.4 GHz)"
}

"State: $($manager.TetheringOperationalState)"
Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
  Where-Object { $_.IPAddress -like '192.168.137.*' } |
  ForEach-Object { "ESP32 gateway / bridge host: $($_.IPAddress)" }
