param([string]$GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\Euro Truck Simulator 2')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
if (Get-Process eurotrucks2 -ErrorAction SilentlyContinue) { throw 'Close ETS2 before installing the signal plugin.' }
$taskBinary = Join-Path $GamePath 'bin\win_x64\eurotrucks2.exe'
if (-not (Test-Path -LiteralPath $taskBinary)) { throw 'ETS2 executable not found. Supply -GamePath.' }
$taskVersion = (Get-Item -LiteralPath $taskBinary).VersionInfo.FileVersion
if ($taskVersion -notmatch '^1\.61\.') { throw "The signal plugin supports ETS2 1.61.x only; found $taskVersion." }
$taskDir = Join-Path $GamePath 'bin\win_x64\plugins'
$taskTarget = Join-Path $taskDir 'ets2la_plugin.dll'
$taskSource = Join-Path $taskRoot 'native\ets2la_plugin.dll'
if (Test-Path -LiteralPath $taskTarget) {
    if ((Get-FileHash -LiteralPath $taskTarget).Hash -eq (Get-FileHash -LiteralPath $taskSource).Hash) { Write-Output 'Signal plugin already installed and current.'; exit 0 }
    throw 'An existing ETS2LA plugin is present. It has not been overwritten.'
}
New-Item -ItemType Directory -Path $taskDir -Force | Out-Null
Copy-Item -LiteralPath $taskSource -Destination $taskTarget
if ((Get-FileHash -LiteralPath $taskTarget).Hash -ne (Get-FileHash -LiteralPath $taskSource).Hash) { throw 'Installed DLL does not match the build.' }
Write-Output "Installed signal plugin: $taskTarget"
