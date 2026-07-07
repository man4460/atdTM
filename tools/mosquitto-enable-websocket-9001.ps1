# Enable Mosquitto WebSocket port 9001 for Cloudflare
# Run PowerShell as Administrator:
#   Set-ExecutionPolicy -Scope Process Bypass -Force
#   & "d:\ESP32\ATD_TM_V3_New_Hier\tools\mosquitto-enable-websocket-9001.ps1"

$ErrorActionPreference = 'Stop'
$confPath = 'C:\Program Files\mosquitto\mosquitto.conf'
$backup = "$confPath.bak-$(Get-Date -Format 'yyyyMMdd-HHmmss')"

if (-not (Test-Path $confPath)) {
  Write-Error "Config not found: $confPath"
}

Copy-Item $confPath $backup -Force
Write-Host "Backup: $backup"

$content = Get-Content $confPath -Raw
$replacement = @'
allow_anonymous true

# port 1883 - local MQTT test
listener 1883 0.0.0.0
protocol mqtt

# port 9001 - Cloudflare WebSocket
listener 9001 0.0.0.0
protocol websockets

# password_file C:\Program Files\mosquitto\password_file.txt

# command ==> mosquitto -v -c mosquitto.conf
'@

# Strip UTF-8 BOM if a previous edit left one (breaks mosquitto line 1)
$bytes = [System.IO.File]::ReadAllBytes($confPath)
if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
  $content = [System.Text.Encoding]::UTF8.GetString($bytes, 3, $bytes.Length - 3)
  Write-Host 'Removed UTF-8 BOM from existing mosquitto.conf'
}

$pattern = '(?s)allow_anonymous true\r?\nlistener 1883\r?\n# password_file.*?\r?\n\r?\n# command ==> mosquitto -v -c mosquitto.conf'
if ($content -match 'listener 9001') {
  Write-Host 'listener 9001 already present - skip config edit'
  $utf8NoBom = New-Object System.Text.UTF8Encoding $false
  [System.IO.File]::WriteAllText($confPath, $content, $utf8NoBom)
} elseif ($content -match $pattern) {
  $content = [regex]::Replace($content, $pattern, $replacement.TrimEnd())
  $utf8NoBom = New-Object System.Text.UTF8Encoding $false
  [System.IO.File]::WriteAllText($confPath, $content, $utf8NoBom)
  Write-Host 'Updated mosquitto.conf'
} else {
  Write-Error 'mosquitto.conf footer does not match expected pattern - edit manually or restore backup'
}

$fwName = 'Mosquitto MQTT WebSocket 9001'
if (-not (Get-NetFirewallRule -DisplayName $fwName -ErrorAction SilentlyContinue)) {
  New-NetFirewallRule -DisplayName $fwName -Direction Inbound -Action Allow -Protocol TCP -LocalPort 9001 | Out-Null
  Write-Host 'Added firewall rule TCP 9001'
}

Restart-Service mosquitto
Start-Sleep -Seconds 2

Write-Host ''
Write-Host '--- Status ---'
Get-Service mosquitto | Format-Table Name, Status -AutoSize
Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue |
  Where-Object { $_.LocalPort -in 1883, 9001 } |
  Select-Object LocalAddress, LocalPort, OwningProcess |
  Format-Table -AutoSize

Write-Host 'Test: Test-NetConnection localhost -Port 9001'
