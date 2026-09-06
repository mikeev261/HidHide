[CmdletBinding()]
Param(
    # Directory containing the CI-built HidHideClient.exe and HidHideCLI.exe.
    [Parameter(Mandatory = $true)]
    [string]$Staging,
    [ValidateSet('x64', 'ARM64')]
    [string]$Platform = 'x64',
    [string]$Out = './artifacts/release',
    [switch]$NoSigning,
    [string]$CertName,
    # Windows SDK signtool.exe, either on PATH or supplied explicitly.
    [string]$SignTool = 'signtool.exe'
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Staging -PathType Container)) { throw 'Staging must be a directory.' }
$stagingPath = (Resolve-Path -LiteralPath $Staging).Path
$outPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)
$separator = [IO.Path]::DirectorySeparatorChar
if ($outPath.TrimEnd($separator).Equals($stagingPath.TrimEnd($separator), [StringComparison]::OrdinalIgnoreCase) -or
    $outPath.StartsWith($stagingPath.TrimEnd($separator) + $separator, [StringComparison]::OrdinalIgnoreCase) -or
    $stagingPath.StartsWith($outPath.TrimEnd($separator) + $separator, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Staging and output directories must be separate and must not contain each other.'
}
foreach ($name in @('HidHideClient.exe', 'HidHideCLI.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $stagingPath $name) -PathType Leaf)) {
        throw "Missing companion executable: $name"
    }
}
if ((Test-Path -LiteralPath $outPath) -and @(Get-ChildItem -LiteralPath $outPath -Force).Count -ne 0) {
    throw 'Output directory must be empty; use a new directory for each release/platform.'
}
if (-not $NoSigning) {
    if ([string]::IsNullOrWhiteSpace($CertName)) { throw 'Specify -CertName for release signing, or -NoSigning for an unsigned package.' }
    $SignTool = (Get-Command $SignTool -CommandType Application -ErrorAction Stop).Source
}
Get-Command dotnet -CommandType Application -ErrorAction Stop | Out-Null

function Sign-ReleaseFile([string]$File) {
    & $SignTool sign /v /n $CertName /tr http://timestamp.digicert.com /fd sha256 /td sha256 $File
    if ($LASTEXITCODE -ne 0) { throw "Signing failed for $File (exit $LASTEXITCODE)." }
    & $SignTool verify /pa /v $File
    if ($LASTEXITCODE -ne 0) { throw "Signature verification failed for $File (exit $LASTEXITCODE)." }
}

# Sign copies before packaging so the embedded executables have the same signatures.
$payloadPath = Join-Path $outPath 'payload'
$msiPath = Join-Path $outPath 'msi'
New-Item -ItemType Directory -Path $payloadPath -Force | Out-Null
New-Item -ItemType Directory -Path $msiPath -Force | Out-Null
foreach ($name in @('HidHideClient.exe', 'HidHideCLI.exe')) {
    $file = Join-Path $payloadPath $name
    Copy-Item -LiteralPath (Join-Path $stagingPath $name) -Destination $file
    if (-not $NoSigning) { Sign-ReleaseFile $file }
}
& dotnet run --project (Join-Path $PSScriptRoot 'Installer/HidHide.Installer.csproj') -c Release -- --staging $payloadPath --out $msiPath --platform $Platform
if ($LASTEXITCODE -ne 0) { throw "Installer build failed (exit $LASTEXITCODE)." }
$packages = @(Get-ChildItem -LiteralPath $msiPath -Filter '*.msi' -File)
if ($packages.Count -ne 1) { throw "Expected one companion MSI, found $($packages.Count)." }
if (-not $NoSigning) { Sign-ReleaseFile $packages[0].FullName }
$packages[0].FullName
