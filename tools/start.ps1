param([switch]$Demo)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskElectron = Join-Path $taskRoot 'node_modules\electron\dist\electron.exe'
if (-not (Test-Path -LiteralPath $taskElectron)) { throw 'Run npm install and node node_modules/electron/install.js first.' }
# The parent Codex environment can run Electron in Node mode. Only the child
# environment is adjusted; the user's persistent variables are untouched.
$taskInfo = [System.Diagnostics.ProcessStartInfo]::new()
$taskInfo.FileName = $taskElectron
$taskInfo.WorkingDirectory = $taskRoot
$taskInfo.UseShellExecute = $false
$taskInfo.CreateNoWindow = $true
$taskInfo.Arguments = '"' + $taskRoot + '"' + $(if ($Demo) { ' --demo' } else { '' })
$taskInfo.EnvironmentVariables.Remove('ELECTRON_RUN_AS_NODE')
$taskProcess = [System.Diagnostics.Process]::Start($taskInfo)
Write-Output "Lane Guide process: $($taskProcess.Id)"
