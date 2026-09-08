[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Payload, [Parameter(Mandatory=$true)][string]$SignTool)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'SignedDriver.psm1') -Force
$manifestPath = Join-Path $PSScriptRoot 'driver-payload.json'
$m = Read-DriverManifest $manifestPath
Assert-SignedDriver $m $Payload $SignTool
$root = Join-Path $PSScriptRoot ('../artifacts/payload-negative-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $root | Out-Null
$script:checks = 1
function Expect-Failure([string]$Name, [scriptblock]$Action, [string]$Message) {
    $failed = $false
    try { & $Action } catch {
        if ($_.Exception.Message -notlike "*$Message*") { throw "Unexpected rejection for ${Name}: $_" }
        $failed = $true
    }
    if (-not $failed) { throw "Negative test accepted $Name" }
    $script:checks++
}
foreach ($name in @('HidHide.inf','HidHide.sys','hidhide.cat','LICENSE.rtf')) {
    $case = Join-Path $root $name
    New-Item -ItemType Directory -Path $case | Out-Null
    foreach ($f in $m.files) { Copy-Item -LiteralPath (Join-Path $Payload $f.name) -Destination $case }
    $target = Join-Path $case $name
    $b = [IO.File]::ReadAllBytes($target); $b[$b.Length-1] = $b[$b.Length-1] -bxor 1
    [IO.File]::WriteAllBytes($target,$b)
    Expect-Failure "tampered $name" { Assert-SignedDriver $m $case $SignTool } 'SHA256 mismatch'
}
Expect-Failure 'missing file' { Assert-DriverHash (Join-Path $root 'missing.sys') $m.files[1].sha256 } 'Missing signed payload'
foreach ($case in @('architecture','duplicate','traversal','coverage','hash')) {
    $bad = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    switch ($case) {
        architecture { $bad.architecture = 'ARM64'; $message='architecture' }
        duplicate { $bad.files[1].name='HidHide.inf'; $message='duplicate' }
        traversal { $bad.files[0].source='../HidHide.inf'; $message='Invalid or duplicate' }
        coverage { $bad.files[0].catalogMember=$false; $message='catalog coverage' }
        hash { $bad.archiveSha256='BAD'; $message='digest' }
    }
    $path=Join-Path $root "$case.json"
    $bad | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $path
    Expect-Failure $case { Read-DriverManifest $path } $message
}
# Alter the INF and its declared digest: hash agreement alone must not pass trust.
$case=Join-Path $root 'catalog-membership'
New-Item -ItemType Directory -Path $case | Out-Null
foreach ($f in $m.files) { Copy-Item -LiteralPath (Join-Path $Payload $f.name) -Destination $case }
Add-Content -LiteralPath (Join-Path $case 'HidHide.inf') '; deliberate membership failure'
$bad=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$bad.files[0].sha256=(Get-FileHash -LiteralPath (Join-Path $case 'HidHide.inf')).Hash
Expect-Failure 'catalog membership despite matching digest' { Assert-SignedDriver $bad $case $SignTool } 'SignTool verification failed'
Write-Output "$script:checks payload checks passed. Evidence: $root"
