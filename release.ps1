[CmdletBinding()]
param(
 [Parameter(Mandatory)][string]$Staging,
 [ValidateSet('x64')][string]$Platform = 'x64',
 [Parameter(Mandatory)][string]$DriverPayload,
 [string]$Out = './artifacts/release',
 [switch]$NoSigning,
 [string]$CertName,
 [string]$SignTool = 'signtool.exe'
)
$ErrorActionPreference = 'Stop'
# Release never downloads an assumed build, publishes, or changes machine trust.
# -NoSigning is an explicit public unsigned-build choice, recorded in the manifest.
if (!$NoSigning -and [string]::IsNullOrWhiteSpace($CertName)) { throw 'Supply -CertName or explicitly choose -NoSigning.' }
$arguments = @{ Staging=$Staging; DriverPayload=$DriverPayload; Out=$Out; SignTool=$SignTool; Sign=(!$NoSigning); CertName=$CertName; VersionedOutput=$true }
& "$PSScriptRoot\build\BuildUnifiedSetup.ps1" @arguments
& "$PSScriptRoot\build\TestUnifiedPreview.ps1" -Msi (Join-Path $Out "HidHide_Profiles_$(([version]([xml](Get-Content "$PSScriptRoot\ProductVersion.props" -Raw)).Project.PropertyGroup.HidHideProductVersion).ToString(3))_x64.msi")
