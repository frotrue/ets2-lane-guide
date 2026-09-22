param([string]$GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\Euro Truck Simulator 2')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
if (Get-Process eurotrucks2 -ErrorAction SilentlyContinue) { throw 'Close ETS2 before installing its telemetry plugin.' }
$taskBinary = Join-Path $GamePath 'bin\win_x64\eurotrucks2.exe'
if (-not (Test-Path -LiteralPath $taskBinary)) { throw 'ETS2 executable not found. Supply -GamePath.' }
$taskPluginDir = Join-Path $GamePath 'bin\win_x64\plugins'
$taskTarget = Join-Path $taskPluginDir 'ets2_lane_guide.dll'
$taskSource = Join-Path $taskRoot 'native\ets2_lane_guide.dll'
if (Test-Path -LiteralPath $taskTarget) {
    if ((Get-FileHash -LiteralPath $taskTarget).Hash -eq (Get-FileHash -LiteralPath $taskSource).Hash) { Write-Output 'Plugin already installed and current.'; exit 0 }
    $taskBackup = $taskTarget + '.backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
    Copy-Item -LiteralPath $taskTarget -Destination $taskBackup
}
New-Item -ItemType Directory -Path $taskPluginDir -Force | Out-Null
Copy-Item -LiteralPath $taskSource -Destination $taskTarget
Write-Output "Installed: $taskTarget"
Write-Output ((Get-FileHash -LiteralPath $taskTarget).Hash)
