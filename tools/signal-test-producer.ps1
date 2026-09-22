# Synthetic integration-test producer; never installed in or attached to a game.
# Run only through check-signal-bridge.cjs with the overlay closed/using old code.
$ErrorActionPreference = 'Stop'
if (Get-Process eurotrucks2,amtrucks -ErrorAction SilentlyContinue) { throw 'Close the game before the shared-memory fixture test.' }
$signalStatusMap = $null; $signalDataMap = $null
$signalStatusView = $null; $signalDataView = $null
try {
    # CreateNew deliberately fails if a real producer or another fixture exists.
    $signalStatusMap = [IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew('Local\ETS2LAPluginStatus', 6)
    $signalDataMap = [IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew('Local\ETS2LASemaphore', 1920)
    $signalStatusView = $signalStatusMap.CreateViewAccessor(0, 6, [IO.MemoryMappedFiles.MemoryMappedFileAccess]::Write)
    $signalDataView = $signalDataMap.CreateViewStream(0, 1920, [IO.MemoryMappedFiles.MemoryMappedFileAccess]::Write)
    $signalStatusView.Write(0, [int]1610)
    [Console]::WriteLine('ready')
    while ($null -ne ($signalLine = [Console]::ReadLine())) {
        if ($signalLine -eq 'stop') { break }
        $signalBytes = [Convert]::FromBase64String($signalLine)
        if ($signalBytes.Length -ne 1920) { throw 'Wrong fixture size.' }
        $signalDataView.Position = 0
        $signalDataView.Write($signalBytes, 0, $signalBytes.Length)
        [Console]::WriteLine('written')
    }
} finally {
    if ($signalDataView) { $signalDataView.Dispose() }
    if ($signalStatusView) { $signalStatusView.Dispose() }
    if ($signalDataMap) { $signalDataMap.Dispose() }
    if ($signalStatusMap) { $signalStatusMap.Dispose() }
}
