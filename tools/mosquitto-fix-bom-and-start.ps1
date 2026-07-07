# Fix UTF-8 BOM in mosquitto.conf and start Mosquitto service
# Run PowerShell as Administrator:
#   Set-ExecutionPolicy -Scope Process Bypass -Force
#   & "d:\ESP32\ATD_TM_V3_New_Hier\tools\mosquitto-fix-bom-and-start.ps1"

$ErrorActionPreference = 'Stop'
$confPath = 'C:\Program Files\mosquitto\mosquitto.conf'

if (-not (Test-Path $confPath)) {
  Write-Error "Config not found: $confPath"
}

$bytes = [System.IO.File]::ReadAllBytes($confPath)
if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
  $bytes = $bytes[3..($bytes.Length - 1)]
  Write-Host 'Removed UTF-8 BOM from mosquitto.conf'
}

$content = [System.Text.Encoding]::UTF8.GetString($bytes)
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($confPath, $content, $utf8NoBom)
Write-Host 'Saved mosquitto.conf as UTF-8 without BOM'

# Quick config smoke test (start broker briefly, then stop)
$testProc = Start-Process -FilePath 'C:\Program Files\mosquitto\mosquitto.exe' `
  -ArgumentList @('-c', $confPath) `
  -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
if ($testProc.HasExited) {
  Write-Error 'mosquitto exited immediately - check Event Viewer or run: mosquitto.exe -c mosquitto.conf -v'
}
Stop-Process -Id $testProc.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Write-Host 'Config smoke test OK'

if ((Get-Service mosquitto).Status -eq 'Running') {
  Restart-Service mosquitto
} else {
  Start-Service mosquitto
}
Start-Sleep -Seconds 2

Write-Host ''
Write-Host '--- Status ---'
Get-Service mosquitto | Format-Table Name, Status -AutoSize
Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue |
  Where-Object { $_.LocalPort -in 1883, 9001 } |
  Select-Object LocalAddress, LocalPort |
  Format-Table -AutoSize

Write-Host 'Test: Test-NetConnection localhost -Port 9001'
