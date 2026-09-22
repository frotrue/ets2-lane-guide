$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskVswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$taskVsRoot = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $taskVsRoot) { throw 'Visual Studio C++ build tools are required.' }
$taskCmake = Join-Path $taskVsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $taskCmake -S "$taskRoot\native\ingame" -B "$taskRoot\native\ingame-build" -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $taskCmake --build "$taskRoot\native\ingame-build" --config Release --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'Native in-game build failed.' }
& (Join-Path (Split-Path $taskCmake) 'ctest.exe') --test-dir "$taskRoot\native\ingame-build" -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Native in-game tests failed.' }
