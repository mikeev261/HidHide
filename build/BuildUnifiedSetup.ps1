[CmdletBinding()]
param(
 [string]$Staging = "$PSScriptRoot\..\bin\Release\x64",
 [Parameter(Mandatory)][string]$DriverPayload,
 [Parameter(Mandatory)][string]$Out,
 [string]$UpstreamRecovery,
 [string]$Companion1Recovery,
 [string]$Companion99Recovery,
 [string]$SignTool = "signtool.exe",
 [switch]$Sign,
 [switch]$VersionedOutput,
 [string]$CertName
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path "$PSScriptRoot\..").Path
Import-Module "$PSScriptRoot\ReleaseEvidence.psm1" -Force
$sourceBefore = Get-SourceEvidence $repo
$buildStartedUtc = [DateTime]::UtcNow.ToString('o')
$Staging = (Resolve-Path -LiteralPath $Staging).Path
$DriverPayload = (Resolve-Path -LiteralPath $DriverPayload).Path
$Out = [IO.Path]::GetFullPath($Out)
if (Test-Path -LiteralPath $Out) { throw 'Use a new output directory; existing recovery payloads are never overwritten.' }
$SignTool = (Get-Command $SignTool -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
if ($Sign -and [string]::IsNullOrWhiteSpace($CertName)) { throw 'Signing requires an operator-supplied certificate subject.' }
Import-Module "$PSScriptRoot\SignedDriver.psm1" -Force
$driverManifest = Read-DriverManifest "$PSScriptRoot\driver-payload.json"
Assert-SignedDriver $driverManifest $DriverPayload $SignTool
$version = ([xml](Get-Content "$repo\ProductVersion.props" -Raw)).Project.PropertyGroup.HidHideProductVersion
$wixVersion = (& wix --version).Trim()
if ($LASTEXITCODE -ne 0 -or $wixVersion.Split('+')[0] -ne '5.0.2') { throw 'This bundle requires verified WiX 5.0.2.' }
$applicationInputs = @('HidHideClient.exe','HidHideCLI.exe') | ForEach-Object {
 $inputPath = Join-Path $Staging $_
 [ordered]@{ path=$inputPath; sha256=(Get-FileHash -LiteralPath $inputPath -Algorithm SHA256).Hash; version=[Diagnostics.FileVersionInfo]::GetVersionInfo($inputPath).FileVersion }
}
function Run([string]$Command, [string[]]$Arguments) { & $Command @Arguments; if ($LASTEXITCODE -ne 0) { throw "$Command failed: $LASTEXITCODE" } }
function SignFile([string]$Path) {
 if (!$Sign) { return }
 Run $SignTool @('sign','/v','/n',$CertName,'/tr','http://timestamp.digicert.com','/fd','sha256','/td','sha256',$Path)
 Run $SignTool @('verify','/pa','/v',$Path)
}
# Work only on copies, including app-local Microsoft runtimes. Never re-sign driver files.
$payload = New-Item -ItemType Directory -Path "$Out\payload"
foreach($name in @('HidHideClient.exe','HidHideCLI.exe')) { Copy-Item -LiteralPath "$Staging\$name" -Destination $payload.FullName }
$Staging = $payload.FullName
& "$repo\build\StageAppRuntime.ps1" -Staging $Staging
foreach($name in @('HidHideClient.exe','HidHideCLI.exe')) { SignFile "$Staging\$name" }
Run dotnet @('build',"$repo\Installer",'-c','Release')
if ($Sign) {
 foreach($name in @('HidHide.Installer.exe','HidHide.DriverSetup.exe')) { SignFile "$repo\Installer\bin\Release\net48\$name" }
}
$previousTool = [Environment]::GetEnvironmentVariable('HIDHIDE_SIGN_TOOL')
$previousCert = [Environment]::GetEnvironmentVariable('HIDHIDE_SIGN_CERT')
try {
 [Environment]::SetEnvironmentVariable('HIDHIDE_SIGN_TOOL',$(if($Sign){$SignTool}else{$null}))
 [Environment]::SetEnvironmentVariable('HIDHIDE_SIGN_CERT',$(if($Sign){$CertName}else{$null}))
 Run dotnet @('run','--no-build','--project',"$repo\Installer",'-c','Release','--','--unified-preview','--staging',$Staging,'--driver-payload',$DriverPayload,'--out',"$Out\msi")
} finally {
 [Environment]::SetEnvironmentVariable('HIDHIDE_SIGN_TOOL',$previousTool)
 [Environment]::SetEnvironmentVariable('HIDHIDE_SIGN_CERT',$previousCert)
}
if ($Sign) { Run $SignTool @('verify','/pa','/v',"$Out\msi\HidHide.Unified.Preview.msi") }
$cache = New-Item -ItemType Directory -Path "$Out\embedded-cache"
foreach ($name in @('HidHide.inf','HidHide.sys','hidhide.cat','LICENSE.rtf')) { Copy-Item -LiteralPath "$DriverPayload\$name" -Destination $cache.FullName }
Copy-Item -LiteralPath "$Out\msi\HidHide.Unified.Preview.msi" -Destination $cache.FullName
foreach($name in @('HidHideCLI.exe','HidHideClient.exe','mfc140u.dll','msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')) { Copy-Item -LiteralPath "$Staging\$name" -Destination $cache.FullName }
if ($UpstreamRecovery) {
 if ((Get-FileHash -LiteralPath $UpstreamRecovery -Algorithm SHA256).Hash -ne 'F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6') { throw 'Unrecognized upstream recovery EXE.' }
 Copy-Item -LiteralPath $UpstreamRecovery -Destination "$cache\UpstreamRecovery.exe"
}
function AddCompanion([string]$Source,[string]$Name,[string]$Code,[string]$ExpectedVersion) {
  if (!$Source) { return }
  $expectedHash = if ($Name -eq 'Companion1Recovery.msi') { 'A9877ED39F5D36998302FBCB2BE94EC8F5C59C5C2990BC41B25A6A772B9431E6' } else { 'EBC9EE898608C3265E59960A9F08EA13DDF4E327D89F33936F6E7ABBC43DE4B4' }
  if ((Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Companion recovery media does not match the verified original file manifest.' }
 $installer = New-Object -ComObject WindowsInstaller.Installer
 $database = $installer.OpenDatabase((Resolve-Path -LiteralPath $Source).Path,0)
 try {
  $props=@{}; $view=$database.OpenView('SELECT `Property`,`Value` FROM `Property`'); $view.Execute()
  while($record=$view.Fetch()) { $props[$record.StringData(1)]=$record.StringData(2); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($record) }
  $view.Close(); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view)
  if ($props.ProductCode -ne $Code -or $props.UpgradeCode -ne '{7078E839-3A07-4FA9-BC3A-7677356C88CF}' -or $props.ProductVersion -ne $ExpectedVersion) { throw 'Unrecognized companion recovery package.' }
  $view=$database.OpenView('SELECT `Cabinet` FROM `Media`'); $view.Execute(); $found=$false
  while($record=$view.Fetch()) { $found=$true; if (!$record.StringData(1).StartsWith('#')) { throw 'Recovery MSI requires external media; complete embedded recovery source required.' }; [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($record) }
  if (!$found) { throw 'Recovery MSI has no embedded payload.' }
  $view.Close(); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view)
 } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer) }
  Copy-Item -LiteralPath $Source -Destination "$cache\$Name"
  if ((Get-FileHash -LiteralPath "$cache\$Name" -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Companion recovery source changed while copying.' }
}
AddCompanion $Companion1Recovery 'Companion1Recovery.msi' '{B7E9D4A2-6F31-4E88-9C0D-1A2B4C4D5E70}' '1.0.0.0'
AddCompanion $Companion99Recovery 'Companion99Recovery.msi' '{B7E9D4A2-6F31-4E88-9C0D-1A2B6C4D5E70}' '99.0.0.0'
$index = @(Get-ChildItem -LiteralPath $cache.FullName -File | Sort-Object Name | ForEach-Object { $_.Name + '|' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash })
[IO.File]::WriteAllLines("$cache\index",$index,[Text.UTF8Encoding]::new($false))
Run dotnet @('build',"$repo\Installer.Controller",'-c','Release',"-p:SetupCacheRoot=$cache",'-t:Rebuild')
Run dotnet @('build',"$repo\Installer.Bootstrapper",'-c','Release')
$ba = "$repo\Installer.Bootstrapper\bin\Release\net48"
$controller = "$repo\Installer.Controller\bin\Release\net48\HidHide.SetupController.exe"
# Freeze the exact BA/controller files used by this artifact. A later ordinary
# project/test build can rebuild the controller without embedded cache resources.
$support = New-Item -ItemType Directory -Path "$Out\support"
Copy-Item -LiteralPath $controller -Destination $support.FullName
foreach($name in @('HidHide.Bootstrapper.exe','HidHide.Bootstrapper.exe.config','mbanative.dll','WixToolset.BootstrapperApplicationApi.dll')) { Copy-Item -LiteralPath "$ba\$name" -Destination $support.FullName }
SignFile "$support\HidHide.SetupController.exe"
SignFile "$support\HidHide.Bootstrapper.exe"
$ba = $support.FullName
$controller = "$support\HidHide.SetupController.exe"
$ns = 'http://wixtoolset.org/schemas/v4/wxs'
$doc = [xml]'<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs" />'
function Element($Parent,[string]$Name,$Attributes) {
 $node=$doc.CreateElement($Name,$ns); foreach($key in $Attributes.Keys) {$node.SetAttribute($key,[string]$Attributes[$key])}; [void]$Parent.AppendChild($node); return $node
}
$bundle=Element $doc.DocumentElement Bundle @{Name='HidHide (mikeev261 fork)';Manufacturer='mikeev261';Tag='HidHide.Upgrade.v1';Version=$version;UpgradeCode='{C62D8280-B0B1-42FD-8969-084CC64F9D5B}'}
$application=Element $bundle BootstrapperApplication @{SourceFile="$ba\HidHide.Bootstrapper.exe"}
foreach($name in @('HidHide.Bootstrapper.exe.config','mbanative.dll','WixToolset.BootstrapperApplicationApi.dll')) { [void](Element $application Payload @{SourceFile="$ba\$name";Name=$name}) }
[void](Element $application Payload @{SourceFile=$controller;Name='HidHide.SetupController.exe'})
[void](Element $application Payload @{SourceFile="$Staging\HidHideCLI.exe";Name='HidHideCLI.exe'})
foreach($name in @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')) { [void](Element $application Payload @{SourceFile="$Staging\$name";Name=$name}) }
[void](Element $bundle Variable @{Name='HidHideTransaction';Type='string';Value='';Hidden='yes'})
$chain=Element $bundle Chain @{DisableSystemRestore='yes'}
$msi=Element $chain MsiPackage @{Id='Unified';SourceFile="$Out\msi\HidHide.Unified.Preview.msi";Visible='no';Compressed='yes';Vital='yes';Cache='force'}
[void](Element $msi MsiProperty @{Name='HIDHIDE_TRANSACTION';Value='[HidHideTransaction]'})

$doc.Save("$Out\HidHide.Setup.wxs")
Run wix @('build',"$Out\HidHide.Setup.wxs",'-arch','x64','-o',"$Out\HidHide.Setup.exe")
if ($Sign) {
 Run wix @('burn','detach',"$Out\HidHide.Setup.exe",'-engine',"$Out\support\engine.exe")
 SignFile "$Out\support\engine.exe"
 Run wix @('burn','reattach',"$Out\HidHide.Setup.exe",'-engine',"$Out\support\engine.exe",'-o',"$Out\HidHide.Setup.Signed.exe")
 SignFile "$Out\HidHide.Setup.Signed.exe"
 Move-Item -LiteralPath "$Out\HidHide.Setup.exe" -Destination "$Out\support\unsigned-bundle.exe"
 Move-Item -LiteralPath "$Out\HidHide.Setup.Signed.exe" -Destination "$Out\HidHide.Setup.exe"
}
$finalName = 'HidHide.Setup.exe'
if ($VersionedOutput) {
 $finalName = "HidHide_$(([version]$version).ToString(3))_x64.exe"
 Move-Item -LiteralPath "$Out\HidHide.Setup.exe" -Destination "$Out\$finalName"
}
# Refuse to label an artifact with sources changed by a concurrent edit/build.
# Prebuilt application inputs have their own hashes; this does not pretend that
# their supplied binaries were necessarily compiled from this source snapshot.
$sourceAfter = Get-SourceEvidence $repo
if ($sourceAfter.Digest -ne $sourceBefore.Digest) { throw 'Sources changed during packaging. Build again after edits finish; no release manifest was produced.' }
$manifest = [ordered]@{
 schemaVersion=1; version=$version; architecture='x64'; artifact=$finalName; signed=[bool]$Sign
 sourceCommit=$sourceBefore.Commit; dirty=$sourceBefore.Dirty; sourceStatus=$sourceBefore.Status; sourceFiles=$sourceBefore.Files; sourceDigest=$sourceBefore.Digest
 buildStartedUtc=$buildStartedUtc; buildFinishedUtc=[DateTime]::UtcNow.ToString('o'); wixVersion=$wixVersion
 applicationInputs=@($applicationInputs); applicationProvenance='Explicit prebuilt inputs; their hashes identify the binaries, not proof of compilation from this source snapshot.'
 recoveryMedia=[ordered]@{ upstream=[bool]$UpstreamRecovery; companion1=[bool]$Companion1Recovery; companion99=[bool]$Companion99Recovery }
 driver=$driverManifest
 files=@(Get-ChildItem -LiteralPath $Out -File -Recurse | ForEach-Object { @{ path=$_.FullName.Substring($Out.Length+1); sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash } })
}
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath "$Out\release-manifest.json" -Encoding UTF8
Assert-SignedDriver $driverManifest $DriverPayload $SignTool
Write-Output "Built unified setup: $Out\$finalName (signed: $([bool]$Sign)). Packaging success does not establish lifecycle release readiness."
