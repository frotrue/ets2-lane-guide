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
    & cl.exe /nologo /LD /O2 /MT /EHsc /W4 /Ivendor/scs-sdk/include native/lane_telemetry.cpp /Fonative/lane_telemetry.obj /link ws2_32.lib /DEF:native/lane_telemetry.def /IMPLIB:native/lane_telemetry.lib /OUT:native/ets2_lane_guide.dll
    if ($LASTEXITCODE -ne 0) { throw 'Telemetry DLL compilation failed.' }
} finally { Pop-Location }
