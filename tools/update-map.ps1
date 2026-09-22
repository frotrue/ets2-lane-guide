param([string]$GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\Euro Truck Simulator 2')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskPreviousSkip = $env:LANE_GUIDE_SKIP_ICONS
Push-Location (Join-Path $taskRoot 'vendor\maps')
try {
    $env:LANE_GUIDE_SKIP_ICONS = '1'
    & node --max-old-space-size=8192 --import tsx packages/clis/parser/index.ts -i $GamePath -o (Join-Path $taskRoot 'data\raw')
    if ($LASTEXITCODE -ne 0) { throw 'Map extraction failed; existing runtime map remains unchanged.' }
    Set-Location $taskRoot
    & node tools/compile-map.cjs
    if ($LASTEXITCODE -ne 0) { throw 'Runtime map compilation failed.' }
    & node tools/verify-map.cjs
    if ($LASTEXITCODE -ne 0) { throw 'Runtime map verification failed.' }
} finally {
    $env:LANE_GUIDE_SKIP_ICONS = $taskPreviousSkip
    Pop-Location
}
