[CmdletBinding()]
param([Parameter(Mandatory)][string]$Msi)
$ErrorActionPreference='Stop'
$wi=New-Object -ComObject WindowsInstaller.Installer
$db=$wi.OpenDatabase((Resolve-Path -LiteralPath $Msi).Path,0)
function Rows([string]$table) {
 $view=$db.OpenView(('SELECT * FROM `'+$table+'`'))
 try {
  [void]$view.Execute()
  while($record=$view.Fetch()) {
   try {
    $count=$record.GetType().InvokeMember('FieldCount','GetProperty',$null,$record,$null)
    [pscustomobject]@{Values=@(for($i=1;$i -le $count;$i++){$record.StringData($i)})}
   } finally {[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($record)}
  }
 } finally {[void]$view.Close(); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view)}
}
$script:checks=0
function Check($condition,[string]$message) {if(!$condition){throw $message}; $script:checks++}
try {
 $summary=$db.SummaryInformation(0)
 try { Check ($summary.Property(7).Split(';')[0] -eq 'x64') 'MSI summary targets x64' }
 finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($summary) }
 $properties=@{}; Rows Property | ForEach-Object {$properties[$_.Values[0]]=$_.Values[1]}
 Check ($properties.ProductName -eq 'HidHide Profiles') 'Product branding'
 Check ($properties.Manufacturer -eq 'HidHide Profiles') 'Publisher'
 Check (!$properties.ContainsKey('ARPSYSTEMCOMPONENT')) 'Public MSI is visible in Installed Apps'
 Check ($properties.MsiLogging -eq 'voicewarmupx') 'Every install records a verbose Windows Installer diagnostic log'
 Check ($properties.UpgradeCode -eq '{A7F7B763-29B4-47FB-9B00-DB18AFA5EB32}') 'Distinct upgrade identity'
 $version=[version]$properties.ProductVersion
 $canonicalVersion='{0}.{1}.{2}.0' -f $version.Major,$version.Minor,$version.Build
 $hash=[Security.Cryptography.SHA256]::Create()
 try { $digest=$hash.ComputeHash([Text.Encoding]::UTF8.GetBytes('a7f7b763-29b4-47fb-9b00-db18afa5eb32:'+$canonicalVersion)) } finally { $hash.Dispose() }
 $expectedCode=[guid]::new([byte[]]$digest[0..15])
 Check ([guid]$properties.ProductCode -eq $expectedCode) 'MSI ProductCode is the explicit version-specific unified identity'
 foreach($name in @('HIDHIDE_TRANSACTION','HIDHIDE_INITIATING_SID','HIDHIDE_OPERATION','HIDHIDE_HELPER_PID','HIDHIDE_RECOVERY')) {
  Check ($properties.SecureCustomProperties.Split(';') -contains $name) ('Secure protected-action property: '+$name)
  Check ($properties.MsiHiddenProperties.Split(';') -contains $name) ('Hidden protected-action property: '+$name)
 }
 Check ($properties.SecureCustomProperties.Split(';') -contains 'HIDHIDE_WINDOWS_BUILD') 'Secure Windows build detection property'
 Check ($properties.SecureCustomProperties.Split(';') -contains 'WIX_NATIVE_MACHINE') 'Secure native-machine detection property crosses the UI-to-execute elevation boundary'
 $dirs=@{}; Rows Directory | ForEach-Object {$dirs[$_.Values[0]]=$_.Values}
 Check ($dirs.INSTALLDIR[1] -eq 'ProgramFiles64Folder' -and $dirs.INSTALLDIR[2] -eq 'HidHide') 'Application installation directory'
 Check ($dirs.Driver[1] -eq 'INSTALLDIR' -and $dirs.Driver[2] -eq 'Driver') 'Driver payload directory'
 # User profile JSON and Electron preferences must never become MSI-owned
 # components: major-upgrade removal may remove every component in the old MSI.
 Check (@($dirs.Values | Where-Object { $_[0] -in @('LocalAppDataFolder','AppDataFolder','PersonalFolder') -or $_[2].Split('|')[-1] -eq 'Profiles' }).Count -eq 0) 'Per-user profiles and editor preferences are outside MSI-owned directories'
 $tables=@(Rows _Tables | ForEach-Object { $_.Values[0] })
 if ($tables -contains 'RemoveFile') {
  foreach ($entry in @(Rows RemoveFile)) {
   Check ($dirs.ContainsKey($entry.Values[3])) ('Removal targets only a declared MSI directory: '+$entry.Values[0])
  }
 }
 Check ($tables -notcontains 'Wix4RemoveFolderEx' -and $tables -notcontains 'WixRemoveFolderEx') 'No recursive folder removal can delete user profile repositories'
 $components=@{}; Rows Component | ForEach-Object {$components[$_.Values[0]]=$_.Values}
 foreach($component in $components.Values) {
  Check (([int]$component[3] -band 256) -ne 0) ('64-bit component: '+$component[0])
 }
 $files=@(Rows File)
 Check (@($files | Where-Object {$_.Values[2].Split('|')[-1] -eq 'HidHideProfiles.exe'}).Count -eq 1) 'Independent Electron editor is packaged'
 Check (@($files | Where-Object {$_.Values[2].Split('|')[-1] -eq 'app.asar'}).Count -eq 1) 'Packaged local editor assets are present'
 function IsEditorDirectory([string]$id) {
  while($id -and $dirs.ContainsKey($id)) {
   if ($dirs[$id][1] -eq 'INSTALLDIR' -and $dirs[$id][2].Split('|')[-1] -eq 'Editor') { return $true }
   $id=$dirs[$id][1]
  }
  return $false
 }
 Check (@($files | Where-Object {!(IsEditorDirectory $components[$_.Values[1]][2])}).Count -eq 10) 'Core native/runtime/driver payload remains exactly ten files'

 foreach($row in $files) {
  $name=$row.Values[2].Split('|')[-1]; $component=$components[$row.Values[1]]
  Check (($name -in @('HidHideCLI.exe','HidHideClient.exe','mfc140u.dll','msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll') -and $component[2] -eq 'INSTALLDIR') -or
         ($name -in @('HidHide.inf','HidHide.sys','hidhide.cat','LICENSE.rtf') -and $component[2] -eq 'Driver') -or (IsEditorDirectory $component[2])) ('Payload destination: '+$name)
 }
 $shortcuts=@(Rows Shortcut); Check ($shortcuts.Count -eq 1 -and $shortcuts[0].Values[4] -eq '[INSTALLDIR]HidHideClient.exe') 'Single enhanced UI shortcut'
 $actions=@{}; Rows CustomAction | ForEach-Object {$actions[$_.Values[0]]=$_.Values}
 $sequence=@{}; Rows InstallExecuteSequence | ForEach-Object {$sequence[$_.Values[0]]=$_.Values}
 $prepareType=[int]$actions.PrepareHidHideUser[1]
 Check (($prepareType -band 0xC00) -eq 0 -and ($prepareType -band 0x800) -eq 0) 'Ordinary-user preparation is immediate and impersonated'
 foreach($name in @('ApplyHidHideDriver','FinalizeHidHideDriver')) {
  $type=[int]$actions[$name][1]
  Check (($type -band 0xC00) -eq 0xC00 -and ($type -band 0x40) -eq 0) ('Checked deferred non-impersonating action: '+$name)
 }
 $undo=[int]$actions.ApplyHidHideDriver_Rollback[1]
 Check (($undo -band 0xD00) -eq 0xD00 -and ($undo -band 0x40) -eq 0) 'Checked deferred rollback action'
 Check ([int]$sequence.ApplyHidHideDriver_Rollback[2] -lt [int]$sequence.ApplyHidHideDriver[2]) 'Rollback scheduled before forward action'
 Check ([int]$sequence.PrepareHidHideUser[2] -lt [int]$sequence.InstallInitialize[2]) 'Ordinary-user handoff precedes elevation transaction'
 Check ([int]$sequence.PrepareHidHideOperation[2] -lt [int]$sequence.CostFinalize[2]) 'Uninstall preserves the registered MSI before first-phase costing'
 Check ([int]$sequence.ApplyHidHideDriver[2] -gt [int]$sequence.InstallFiles[2] -and [int]$sequence.ApplyHidHideDriver[2] -lt [int]$sequence.ForceReboot[2]) 'Driver apply follows payload installation and precedes reboot suspension'
 Check ([int]$sequence.FinalizeHidHideDriver[2] -gt [int]$sequence.ForceReboot[2] -and [int]$sequence.FinalizeHidHideDriver[2] -lt [int]$sequence.InstallFinalize[2]) 'Post-reboot verification precedes MSI commit'
 Check ($sequence.ForceReboot[1] -eq 'NOT AFTERREBOOT AND NOT UPGRADINGPRODUCTCODE AND NOT HIDHIDE_RECOVERY AND NOT WIX_UPGRADE_DETECTED AND (NOT Installed OR REINSTALL OR HIDHIDE_UNINSTALL)') 'Reboot stages uninstall and excludes continuation and application-only upgrade removal'
 Check ($properties.SecureCustomProperties.Split(';') -contains 'HIDHIDE_UNINSTALL') 'Staged uninstall survives MSI elevation'
 foreach($name in @('PrepareHidHideOperation','PrepareHidHideUser','ApplyHidHideDriver','FinalizeHidHideDriver')) { Check ($sequence[$name][1] -eq 'NOT UPGRADINGPRODUCTCODE') ('Major-upgrade removal skips direct lifecycle action: '+$name) }
 Check ([int]$sequence.RemoveExistingProducts[2] -gt [int]$sequence.InstallInitialize[2] -and [int]$sequence.RemoveExistingProducts[2] -lt [int]$sequence.ProcessComponents[2]) 'Major upgrade removes old MSI inside rollback transaction before component processing'
 $upgrades=@(Rows Upgrade)
 Check (@($upgrades | Where-Object {$_.Values[0] -eq $properties.UpgradeCode -and $_.Values[6] -eq 'WIX_UPGRADE_DETECTED'}).Count -eq 1) 'Major upgrade detects older unified MSI'
 Check (@($upgrades | Where-Object {$_.Values[0] -eq $properties.UpgradeCode -and $_.Values[6] -eq 'WIX_DOWNGRADE_DETECTED' -and ([int]$_.Values[4] -band 2) -ne 0}).Count -eq 1) 'Newer unified MSI detection is non-removing'
 $launch=@(Rows LaunchCondition)
 $platformCondition='Installed OR (WIX_NATIVE_MACHINE = 34404 AND HIDHIDE_WINDOWS_BUILD >= 22000 AND MsiNTProductType = 1)'
 Check (@($launch | Where-Object {$_.Values[0] -eq $platformCondition}).Count -eq 1) 'Windows 11 x64 launch condition uses native architecture and real build detection'
 Check (@($launch | Where-Object {$_.Values[0] -match 'VersionNT64\s*[><=]'}).Count -eq 0) 'Launch conditions do not compare the VersionNT64 compatibility value'
 $appSearch=@{}; Rows AppSearch | ForEach-Object {$appSearch[$_.Values[0]]=$_.Values[1]}
 Check ($appSearch.HIDHIDE_WINDOWS_BUILD) 'Windows build property is populated by AppSearch'
 $regLocator=@{}; Rows RegLocator | ForEach-Object {$regLocator[$_.Values[0]]=$_.Values}
 $buildLocator=$regLocator[$appSearch.HIDHIDE_WINDOWS_BUILD]
 Check ($buildLocator[1] -eq 2 -and $buildLocator[2] -eq 'SOFTWARE\Microsoft\Windows NT\CurrentVersion' -and $buildLocator[3] -eq 'CurrentBuildNumber' -and (([int]$buildLocator[4] -band 16) -ne 0)) 'Windows build search reads the 64-bit CurrentBuildNumber registry value'
 $uiSequence=@{}; Rows InstallUISequence | ForEach-Object {$uiSequence[$_.Values[0]]=$_.Values}
 Check ([int]$uiSequence.AppSearch[2] -lt [int]$uiSequence.LaunchConditions[2] -and [int]$sequence.AppSearch[2] -lt [int]$sequence.LaunchConditions[2]) 'Windows build detection precedes launch conditions in UI and execute sequences'
 $nativeAction=@($actions.Keys | Where-Object {$_ -match 'QueryNativeMachine'})
 Check ($nativeAction.Count -eq 1) 'Native machine detection custom action is present'
 Check ([int]$uiSequence[$nativeAction[0]][2] -lt [int]$uiSequence.LaunchConditions[2] -and [int]$sequence[$nativeAction[0]][2] -lt [int]$sequence.LaunchConditions[2]) 'Native machine detection precedes launch conditions in UI and execute sequences'
 $session=$wi.OpenPackage((Resolve-Path -LiteralPath $Msi).Path,1)
 try {
  function PlatformResult([string]$native,[string]$build,[string]$productType,[string]$installed='') {
   $session.Property('WIX_NATIVE_MACHINE')=$native
   $session.Property('HIDHIDE_WINDOWS_BUILD')=$build
   $session.Property('MsiNTProductType')=$productType
   $session.Property('Installed')=$installed
   return [int]$session.EvaluateCondition($platformCondition)
  }
  $session.Property('WIX_UPGRADE_DETECTED')='{OLD-UNIFIED-PRODUCT}'
  $session.Property('Installed')=''
  Check ([int]$session.EvaluateCondition($sequence.ForceReboot[1]) -eq 0) 'Actual MSI condition skips forced reboot on application upgrade'
  $session.Property('WIX_UPGRADE_DETECTED')=''
  $session.Property('UPGRADINGPRODUCTCODE')='{NEW-UNIFIED-PRODUCT}'
  foreach($name in @('PrepareHidHideOperation','PrepareHidHideUser','ApplyHidHideDriver','FinalizeHidHideDriver','ForceReboot')) {
   Check ([int]$session.EvaluateCondition($sequence[$name][1]) -eq 0) ('Old-product upgrade removal retains driver and skips restart: '+$name)
  }
  $session.Property('UPGRADINGPRODUCTCODE')=''
  Check ((PlatformResult '34404' '22000' '1') -eq 1) 'Windows 11 x64 minimum build passes the platform gate'
  Check ((PlatformResult '34404' '26200' '1') -eq 1) 'Current Windows 11 x64 build passes the platform gate'
  Check ((PlatformResult '34404' '19045' '1') -eq 0) 'Windows 10 x64 fails the platform gate'
  Check ((PlatformResult '43620' '26200' '1') -eq 0) 'Windows 11 ARM64 fails the x64 platform gate'
  Check ((PlatformResult '34404' '26200' '3') -eq 0) 'Windows Server fails the client platform gate'
  Check ((PlatformResult '' '' '' '{PRODUCT-CODE}') -eq 1) 'Installed product can always enter maintenance for removal'
 } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($session) }
 Check (@($launch | Where-Object {$_.Values[0] -eq 'UILevel >= 3 OR AFTERREBOOT OR UPGRADINGPRODUCTCODE'}).Count -eq 1) 'Interactive UI prevents unprompted ForceReboot'
 Check (@($launch | Where-Object {$_.Values[0] -match 'HIDHIDE_TRANSACTION'}).Count -eq 0) 'Public MSI has no private-bootstrapper launch gate'
 $dialogs=@(Rows Dialog)
 # Word-processor RTF can leave the first WixUI_Minimal license page blank
 # until scrolled. Inspect the actual MSI control, not just the source file.
 $licenses=@(Rows Control | Where-Object { $_.Values[0] -eq 'WelcomeEulaDlg' -and $_.Values[2] -eq 'ScrollableText' })
 Check ($licenses.Count -eq 1) 'Welcome page contains exactly one license control'
 $licenseRtf=$licenses[0].Values[9]
 Check ($licenseRtf -notmatch '\\(?:stylesheet|fontemb|pict|object|pgdsctbl|sectd|paperw|paperh)\b') 'License RTF excludes complex document layout that can prevent initial paint'
 Add-Type -AssemblyName System.Windows.Forms
 $licenseReader=New-Object System.Windows.Forms.RichTextBox
 try {
  $licenseReader.Rtf=$licenseRtf
  $expectedLicense=[IO.File]::ReadAllText((Join-Path $PSScriptRoot '..\LICENSE'))
  Check (($licenseReader.Text -replace '\s+',' ').Trim() -ceq ($expectedLicense -replace '\s+',' ').Trim()) 'Displayed MSI license preserves the full canonical MIT text and both copyright notices'
 } finally { $licenseReader.Dispose() }
 foreach($name in @('WelcomeEulaDlg','ProgressDlg','ExitDialog','MaintenanceWelcomeDlg','MaintenanceTypeDlg','VerifyReadyDlg')) {
  Check (@($dialogs | Where-Object {$_.Values[0] -eq $name}).Count -eq 1) ('Standard WiX MSI dialog present: '+$name)
 }
 "$script:checks public MSI structure and standard-dialog checks passed. This does not execute lifecycle actions."
} finally {
 [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($db)
 [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($wi)
}
