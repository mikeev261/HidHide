[CmdletBinding()]
param(
    [ValidateRange(0, 3600)][int]$WarmupSeconds = 60,
    [ValidateRange(1, 3600)][int]$MeasureSeconds = 600,
    [ValidateSet('empty', 'twenty', 'one-match', 'competing', 'churn', 'reconnect', 'editor', 'minimized', 'tray')]
    [string[]]$Workloads = @('empty', 'twenty', 'one-match', 'competing', 'churn', 'reconnect', 'editor', 'minimized', 'tray'),
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\artifacts\profiles-performance.json')
)

$ErrorActionPreference = 'Stop'
$client = (Resolve-Path (Join-Path $PSScriptRoot '..\bin\Release\x64\HidHideClient.exe')).Path
$temporary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$logicalProcessors = [Environment]::ProcessorCount
$os = Get-CimInstance Win32_OperatingSystem
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$results = @()

function Get-Mean([double[]]$Values) {
    if (!$Values.Count) { return 0.0 }
    return ($Values | Measure-Object -Average).Average
}

function Get-StandardDeviation([double[]]$Values) {
    if ($Values.Count -lt 2) { return 0.0 }
    $mean = Get-Mean $Values
    $sum = 0.0
    foreach ($value in $Values) { $sum += [Math]::Pow($value - $mean, 2) }
    return [Math]::Sqrt($sum / ($Values.Count - 1))
}

foreach ($workload in $Workloads) {
    $record = $null
    $token = [Guid]::NewGuid().ToString('D')
    $repositoryRoot = Join-Path $temporary "HidHide-Profiles-Restart-Test-$token"
    $readyName = "Local\HidHide.Performance.Ready.$token"
    $commandName = "Local\HidHide.Performance.Command.$token"
    $completedName = "Local\HidHide.Performance.Completed.$token"
    $ready = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, $readyName)
    $command = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, $commandName)
    $completed = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, $completedName)
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $client
    $start.Arguments = "--profile-restart-test perf-$workload `"$repositoryRoot`" $readyName $commandName $completedName"
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $profileProcess = [Diagnostics.Process]::Start($start)
    try {
        if (!$ready.WaitOne(15000) -or !$completed.WaitOne(15000)) { throw "The isolated $workload workload did not become ready" }
        Start-Sleep -Seconds $WarmupSeconds
        $profileProcess.Refresh()
        $cpuStart = $profileProcess.TotalProcessorTime.TotalSeconds
        $clock = [Diagnostics.Stopwatch]::StartNew()
        $previousCpu = $cpuStart
        $previousWall = 0.0
        [double[]]$cpuCoreSamples = @()
        [double[]]$privateMiBSamples = @()
        while ($clock.Elapsed.TotalSeconds -lt $MeasureSeconds) {
            Start-Sleep -Seconds 1
            $profileProcess.Refresh()
            if ($profileProcess.HasExited) { throw "The isolated $workload workload exited during measurement" }
            $wall = $clock.Elapsed.TotalSeconds
            $currentCpu = $profileProcess.TotalProcessorTime.TotalSeconds
            $interval = [Math]::Max(0.001, $wall - $previousWall)
            $cpuCoreSamples += (($currentCpu - $previousCpu) / $interval) * 100.0
            $privateMiBSamples += $profileProcess.PrivateMemorySize64 / 1MB
            $previousCpu = $currentCpu
            $previousWall = $wall
        }
        $clock.Stop()
        $profileProcess.Refresh()
        $oneCore = (($profileProcess.TotalProcessorTime.TotalSeconds - $cpuStart) / $clock.Elapsed.TotalSeconds) * 100.0
        $record = [pscustomobject]@{
            workload = $workload
            productionProcessTree = @('HidHideClient.exe (UI + in-process coordinator)')
            repository = 'isolated temporary JSON repository'
            enforcement = 'deterministic in-process no-op adapter; live driver unopened'
            syntheticInput = switch ($workload) {
                'empty' { 'required Default Global only; zero application profiles; minimized window; no synthetic process or device changes' }
                'twenty' { '20 enabled application profiles; no matching processes; minimized window' }
                'one-match' { '20 enabled profiles; one stable exact-path verified process match; minimized window' }
                'competing' { '20 enabled profiles; two stable verified matches with deterministic priority winner; minimized window' }
                'churn' { 'exact-path process presence toggled every 137 ms' }
                'reconnect' { 'device notifications emitted every 100 ms with synthetic connect/disconnect changes through the production debounce pipeline' }
                'editor' { '20 enabled profiles; editor visible; no synthetic process or device changes' }
                'minimized' { '20 enabled profiles; manager minimized; no synthetic process or device changes' }
                'tray' { '20 enabled profiles; manager hidden to tray; no synthetic process or device changes' }
            }
            warmupSeconds = $WarmupSeconds
            measuredSeconds = [Math]::Round($clock.Elapsed.TotalSeconds, 3)
            samples = $cpuCoreSamples.Count
            cpuPercentOneLogicalCore = [Math]::Round($oneCore, 6)
            cpuPercentWholeMachine = [Math]::Round($oneCore / $logicalProcessors, 6)
            intervalCpuOneCoreMean = [Math]::Round((Get-Mean $cpuCoreSamples), 6)
            intervalCpuOneCoreStdDev = [Math]::Round((Get-StandardDeviation $cpuCoreSamples), 6)
            privateMiBMean = [Math]::Round((Get-Mean $privateMiBSamples), 3)
            privateMiBMax = [Math]::Round(($privateMiBSamples | Measure-Object -Maximum).Maximum, 3)
            privateMiBStdDev = [Math]::Round((Get-StandardDeviation $privateMiBSamples), 3)
            privateMiBStart = [Math]::Round($privateMiBSamples[0], 3)
            privateMiBEnd = [Math]::Round($privateMiBSamples[-1], 3)
            privateMiBGrowth = [Math]::Round($privateMiBSamples[-1] - $privateMiBSamples[0], 3)
        }
        $results += $record
    }
    finally {
        $command.Set() | Out-Null
        if (!$profileProcess.HasExited) { $profileProcess.WaitForExit(15000) | Out-Null }
        if (!$profileProcess.HasExited) { $profileProcess.Kill($true); $profileProcess.WaitForExit() }
        $counterPath = Join-Path $repositoryRoot '.performance-counters.txt'
        if ($record -and (Test-Path -LiteralPath $counterPath)) {
            $activity = Get-Content -LiteralPath $counterPath -Raw | ConvertFrom-Json
            $verified = switch ($workload) {
                'empty' { $activity.processScans -eq 0 }
                { $_ -in @('twenty', 'editor', 'minimized') } { $activity.processScans -gt 0 }
                'one-match' { $activity.processScans -gt 0 -and $activity.matchingObservations -gt 0 }
                'competing' { $activity.processScans -gt 0 -and $activity.matchingObservations -ge 2 }
                'churn' { $activity.syntheticTransitions -gt 0 -and $activity.matchingObservations -gt 0 }
                'reconnect' { $activity.deviceNotifications -gt 0 -and $activity.deviceRefreshes -gt 0 }
                'tray' { $activity.trayPath -and $activity.trayIconAccepted -and $activity.processScans -gt 0 }
            }
            $record | Add-Member -NotePropertyName activityCounters -NotePropertyValue $activity
            $record | Add-Member -NotePropertyName activityVerified -NotePropertyValue ([bool]$verified)
        } elseif ($record) {
            $record | Add-Member -NotePropertyName activityCounters -NotePropertyValue $null
            $record | Add-Member -NotePropertyName activityVerified -NotePropertyValue $false
        }
        $profileProcess.Dispose(); $ready.Dispose(); $command.Dispose(); $completed.Dispose()
        $resolvedRoot = [IO.Path]::GetFullPath($repositoryRoot)
        if ($resolvedRoot.StartsWith($temporary, [StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolvedRoot).StartsWith('HidHide-Profiles-Restart-Test-', [StringComparison]::Ordinal)) {
            Remove-Item -LiteralPath $resolvedRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    if ($record -and !$record.activityVerified) { throw "The isolated $workload workload did not prove its intended activity" }
}

$empty = $results | Where-Object workload -eq 'empty' | Select-Object -First 1
foreach ($result in $results) {
    $delta = if ($empty) { [Math]::Round($result.privateMiBMean - $empty.privateMiBMean, 3) } else { $null }
    $result | Add-Member -NotePropertyName privateMiBDeltaVsEmpty -NotePropertyValue $delta
    $result | Add-Member -NotePropertyName withinTenMiBOfEmpty -NotePropertyValue $(if ($null -eq $delta) { $null } else { $delta -le 10.0 })
}

$evidence = [ordered]@{
    capturedUtc = [DateTime]::UtcNow.ToString('o')
    executable = $client
    executableSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $client).Hash
    os = $os.Caption
    osVersion = $os.Version
    processor = $cpu.Name.Trim()
    logicalProcessors = $logicalProcessors
    methodology = 'Each workload ran alone. After the warm-up, CPU time and private bytes were sampled once per second. One-core CPU is process CPU seconds divided by wall seconds; whole-machine CPU divides that result by the logical-processor count.'
    memoryComparison = 'Additional private memory is each workload mean minus the isolated empty-catalog mean. The <=10 MiB gate is reported only when the empty workload is included in the same run.'
    comparison = 'No safe equivalent prior-build isolated mode exists; prior-build regression and installed/live-driver comparison remain pending user-authorized acceptance.'
    results = $results
}
$parent = Split-Path -Parent $OutputPath
if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
$evidence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding utf8
$evidence | ConvertTo-Json -Depth 8
