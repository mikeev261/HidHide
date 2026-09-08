[CmdletBinding()]
param(
 [Parameter(Mandatory)][string]$Staging,
 [ValidateSet('x64')][string]$Platform = 'x64',
 [Parameter(Mandatory)][string]$DriverPayload,
 [Parameter(Mandatory)][string]$UpstreamRecovery,
 [Parameter(Mandatory)][string]$Companion1Recovery,
 [Parameter(Mandatory)][string]$Companion99Recovery,
 [string]$Out = './artifacts/release',
 [switch]$NoSigning,
 [string]$CertName,
 [string]$SignTool = 'signtool.exe'
)
$ErrorActionPreference = 'Stop'
# Release never downloads an assumed build, publishes, or changes machine trust.
# -NoSigning is an explicit public unsigned-build choice, recorded in the manifest.
if (!$NoSigning -and [string]::IsNullOrWhiteSpace($CertName)) { throw 'Supply -CertName or explicitly choose -NoSigning.' }
$arguments = @{ Staging=$Staging; DriverPayload=$DriverPayload; UpstreamRecovery=$UpstreamRecovery; Out=$Out; SignTool=$SignTool; Sign=(!$NoSigning); CertName=$CertName; VersionedOutput=$true }
if ($Companion1Recovery) { $arguments.Companion1Recovery=$Companion1Recovery }
if ($Companion99Recovery) { $arguments.Companion99Recovery=$Companion99Recovery }
& "$PSScriptRoot\build\BuildUnifiedSetup.ps1" @arguments
& "$PSScriptRoot\build\TestUnifiedPreview.ps1" -Msi "$Out\msi\HidHide.Unified.Preview.msi"
