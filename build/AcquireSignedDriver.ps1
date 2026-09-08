[CmdletBinding()]
param(
    [string]$Manifest = (Join-Path $PSScriptRoot 'driver-payload.json'),
    [string]$Cache = (Join-Path $PSScriptRoot '../artifacts/driver-cache'),
    [Parameter(Mandatory=$true)][string]$Out,
    [Parameter(Mandatory=$true)][string]$SignTool
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'SignedDriver.psm1') -Force
$m = Read-DriverManifest $Manifest
$SignTool = (Get-Command $SignTool -CommandType Application -ErrorAction Stop).Source
$outPath = [IO.Path]::GetFullPath($Out)
if (Test-Path -LiteralPath $outPath) { throw 'Signed payload output must be a new directory; stale staging is not accepted.' }
$cachePath = [IO.Path]::GetFullPath($Cache)
New-Item -ItemType Directory -Path $cachePath -Force | Out-Null
$archive = Join-Path $cachePath $m.archiveName
if (-not (Test-Path -LiteralPath $archive)) {
    $temporary = Join-Path $cachePath ([guid]::NewGuid().ToString() + '.download')
    Invoke-WebRequest -UseBasicParsing -Uri $m.url -OutFile $temporary
    Assert-DriverHash $temporary $m.archiveSha256
    Move-Item -LiteralPath $temporary -Destination $archive
}
Assert-DriverHash $archive $m.archiveSha256
Invoke-DriverVerification $SignTool @('verify','/pa','/v',$archive)
$signature = Get-AuthenticodeSignature -LiteralPath $archive
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -cne $m.archiveSignerThumbprint) { throw 'Unexpected archive signing identity.' }

# Unique extraction directory; execute only the reviewed extraction interface.
$extract = Join-Path $cachePath ('extract-' + [guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path $extract | Out-Null
$process = Start-Process -FilePath $archive -ArgumentList @('/extract',('"' + $extract + '"')) -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(60000)) { $process.Kill(); throw 'Extraction timed out; no payload staged.' }
if ($process.ExitCode -ne 0) { throw "Extraction failed: $($process.ExitCode)" }
foreach ($f in $m.files) { Assert-DriverHash (Join-Path $extract $f.source) $f.sha256 }
$source = Split-Path (Join-Path $extract $m.files[0].source)
Assert-SignedDriver $m $source $SignTool

# Stage only after all validation passes; validate again after copying.
New-Item -ItemType Directory -Path $outPath | Out-Null
foreach ($f in $m.files) { Copy-Item -LiteralPath (Join-Path $extract $f.source) -Destination (Join-Path $outPath $f.name) }
Assert-SignedDriver $m $outPath $SignTool
Copy-Item -LiteralPath $Manifest -Destination (Join-Path $outPath 'driver-payload.json')
Write-Output $outPath
