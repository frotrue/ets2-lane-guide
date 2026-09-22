$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskVswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$taskVsRoot = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $taskVsRoot) { throw 'Visual Studio C++ build tools are required.' }
$taskCmake = Join-Path $taskVsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $taskCmake)) { throw 'Visual Studio CMake tools are required.' }
$taskSource = Join-Path $taskRoot 'vendor\ets2la-plugin'
$taskBuild = Join-Path $taskRoot 'native\ets2la-build'
& $taskCmake -S $taskSource -B $taskBuild -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'ETS2LA signal source configuration failed.' }
& $taskCmake --build $taskBuild --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'ETS2LA signal source compilation failed.' }
Copy-Item -LiteralPath (Join-Path $taskBuild 'Release\ets2la_plugin.dll') -Destination (Join-Path $taskRoot 'native\ets2la_plugin.dll')
Write-Output 'Built native/ets2la_plugin.dll (input overrides disabled in local source).'
