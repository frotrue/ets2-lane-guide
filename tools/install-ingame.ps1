param([string]$GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\Euro Truck Simulator 2')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
if (Get-Process eurotrucks2 -ErrorAction SilentlyContinue) { throw 'Close ETS2 before installing the in-game DLL.' }
$taskExe = Join-Path $GamePath 'bin\win_x64\eurotrucks2.exe'
if (-not (Test-Path -LiteralPath $taskExe)) { throw 'ETS2 x64 executable not found.' }
$taskInfo = (Get-Item -LiteralPath $taskExe).VersionInfo
$taskVersion = '{0}.{1}.{2}.{3}' -f $taskInfo.FileMajorPart,$taskInfo.FileMinorPart,$taskInfo.FileBuildPart,$taskInfo.FilePrivatePart
if ($taskVersion -ne '1.61.1.0') { throw "This map was built for ETS2 1.61.1.0; found $taskVersion. Rebuild and validate the map first." }
$taskDll = Join-Path $taskRoot 'native\ingame-build\Release\ets2_lane_ingame.dll'
$taskMap = Join-Path $taskRoot 'data\ingame-map.bin'
$taskProducer = Join-Path $taskRoot 'native\ets2la_plugin.dll'
foreach ($taskFile in @($taskDll,$taskMap,$taskProducer)) { if (-not (Test-Path -LiteralPath $taskFile)) { throw "Missing build artifact: $taskFile" } }
$taskPlugins = Join-Path $GamePath 'bin\win_x64\plugins'
$taskData = Join-Path $taskPlugins 'lane_guide'
New-Item -ItemType Directory -Path $taskData -Force | Out-Null
$taskTargetDll = Join-Path $taskPlugins 'ets2_lane_ingame.dll'
$taskTargetMap = Join-Path $taskData 'map.bin'
$taskTargetProducer = Join-Path $taskPlugins 'ets2la_plugin.dll'
$taskBackup = Join-Path $taskRoot ('output\ingame-backup\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
foreach ($taskExisting in @($taskTargetDll,$taskTargetMap,$taskTargetProducer)) {
    if (Test-Path -LiteralPath $taskExisting) {
        New-Item -ItemType Directory -Path $taskBackup -Force | Out-Null
        Copy-Item -LiteralPath $taskExisting -Destination $taskBackup
    }
}
Copy-Item -LiteralPath $taskMap -Destination $taskTargetMap -Force
Copy-Item -LiteralPath $taskDll -Destination $taskTargetDll -Force
Copy-Item -LiteralPath $taskProducer -Destination $taskTargetProducer -Force
Copy-Item -LiteralPath (Join-Path $taskRoot 'vendor\imgui\LICENSE.txt') -Destination (Join-Path $taskData 'imgui-LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $taskRoot 'vendor\minhook\LICENSE.txt') -Destination (Join-Path $taskData 'minhook-LICENSE.txt') -Force
Get-FileHash -Algorithm SHA256 -LiteralPath $taskTargetDll,$taskTargetMap,$taskTargetProducer | Select-Object Path,Hash
Write-Output 'Installed native in-game HUD. No external application, startup task, service, registry entry, or launcher was created.'
