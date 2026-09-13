[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Staging,
    [string]$VisualStudio
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($VisualStudio)) {
    $locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path -LiteralPath $locator -PathType Leaf)) { throw 'Specify -VisualStudio or install the Visual Studio locator.' }
    $VisualStudio = & $locator -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($VisualStudio)) { throw 'Visual Studio C++ runtime source not found.' }
    $VisualStudio = $VisualStudio.Trim()
}
# App-local redistribution from the licensed Visual Studio Redist directory.
# Windows 11 supplies UCRT. Update these pins with the compiler/toolchain and
# inspect both ordinary and delay imports before changing the runtime set.
$files = @(
    @{ Name='msvcp140.dll'; Directory='14.52.36328\x64\Microsoft.VC145.CRT'; Hash='28856EF7D5C4426A4A873237D52DC008B305BE7C344F070CDD4C9E9B2DD490B1' },
    @{ Name='vcruntime140.dll'; Directory='14.52.36328\x64\Microsoft.VC145.CRT'; Hash='A20E1AF60588284DAAC7B5230C5EBE7F1224DA982FD13B95018A985881A2F7B5' },
    @{ Name='vcruntime140_1.dll'; Directory='14.52.36328\x64\Microsoft.VC145.CRT'; Hash='FC3DEB4A1BFEE6F0F20B1F93F46A1BDCA037A6B79B442CCA661C8CBB5712F74C' },
    @{ Name='mfc140u.dll'; Directory='14.51.36231\x64\Microsoft.VC145.MFC'; Hash='2C885830725AF4CEB53B32D374B630B1DE2122DF22D767D671639F6C3F291B4F' }
)
$destination = (Resolve-Path -LiteralPath $Staging).Path
foreach ($entry in $files) {
    $source = Join-Path $VisualStudio ('VC\Redist\MSVC\' + $entry.Directory + '\' + $entry.Name)
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $entry.Hash) { throw "Unrecognized runtime: $source" }
    $signature = Get-AuthenticodeSignature -LiteralPath $source
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)') { throw "Runtime signature did not verify: $source" }
    $bytes = [IO.File]::ReadAllBytes($source)
    $header = [BitConverter]::ToInt32($bytes, 60)
    if ($header -lt 64 -or $header + 6 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes,$header) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes,$header+4) -ne 0x8664) { throw "Runtime is not an AMD64 PE file: $source" }
}
# Verify the complete source set before changing staging.
foreach ($entry in $files) {
    $source = Join-Path $VisualStudio ('VC\Redist\MSVC\' + $entry.Directory + '\' + $entry.Name)
    $target = Join-Path $destination $entry.Name
    Copy-Item -LiteralPath $source -Destination $target -Force
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $entry.Hash) { throw "Staged runtime changed: $target" }
    Write-Output ($entry.Name + ' SHA256 ' + $entry.Hash)
}
