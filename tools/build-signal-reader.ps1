$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
Push-Location $taskRoot
try {
    if (-not $env:INCLUDE) {
        $taskVswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
        $taskVsRoot = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not $taskVsRoot) { throw 'Visual Studio C++ build tools are required.' }
        & "$taskVsRoot\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    }
    & cl.exe /nologo /std:c++17 /O2 /MT /EHsc /W4 native/traffic_signal_reader.cpp /Fonative/traffic_signal_reader.obj /Fenative/traffic_signal_reader.exe
    if ($LASTEXITCODE -ne 0) { throw 'Read-only signal bridge compilation failed.' }
} finally { Pop-Location }
