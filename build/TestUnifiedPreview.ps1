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
 Check ($properties.ProductName -eq 'HidHide (mikeev261 fork)') 'Product branding'
 Check ($properties.Manufacturer -eq 'mikeev261') 'Publisher'
 Check ($properties.ARPSYSTEMCOMPONENT -eq '1') 'Private MSI visibility'
 Check ($properties.UpgradeCode -eq '{A7F7B763-29B4-47FB-9B00-DB18AFA5EB32}') 'Distinct upgrade identity'
 $version=[version]$properties.ProductVersion
 $canonicalVersion='{0}.{1}.{2}.0' -f $version.Major,$version.Minor,$version.Build
 $hash=[Security.Cryptography.SHA256]::Create()
 try { $digest=$hash.ComputeHash([Text.Encoding]::UTF8.GetBytes('a7f7b763-29b4-47fb-9b00-db18afa5eb32:'+$canonicalVersion)) } finally { $hash.Dispose() }
 $expectedCode=[guid]::new([byte[]]$digest[0..15])
 Check ([guid]$properties.ProductCode -eq $expectedCode) 'MSI ProductCode is the explicit version-specific unified identity'
 Check ($properties.SecureCustomProperties.Split(';') -contains 'HIDHIDE_TRANSACTION') 'Secure transaction property'
 $dirs=@{}; Rows Directory | ForEach-Object {$dirs[$_.Values[0]]=$_.Values}
 Check ($dirs.INSTALLDIR[1] -eq 'ProgramFiles64Folder' -and $dirs.INSTALLDIR[2] -eq 'HidHide') 'Application installation directory'
 Check ($dirs.Driver[1] -eq 'INSTALLDIR' -and $dirs.Driver[2] -eq 'Driver') 'Driver payload directory'
 $components=@{}; Rows Component | ForEach-Object {$components[$_.Values[0]]=$_.Values}
 foreach($component in $components.Values) {
  Check (([int]$component[3] -band 256) -ne 0) ('64-bit component: '+$component[0])
 }
 $files=@(Rows File); Check ($files.Count -eq 10) 'Expected two applications, four runtime DLLs and four original driver/license files'
 foreach($row in $files) {
  $name=$row.Values[2].Split('|')[-1]; $component=$components[$row.Values[1]]
  Check (($name -in @('HidHideCLI.exe','HidHideClient.exe','mfc140u.dll','msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll') -and $component[2] -eq 'INSTALLDIR') -or
         ($name -in @('HidHide.inf','HidHide.sys','hidhide.cat','LICENSE.rtf') -and $component[2] -eq 'Driver')) ('Payload destination: '+$name)
 }
 $shortcuts=@(Rows Shortcut); Check ($shortcuts.Count -eq 1 -and $shortcuts[0].Values[4] -eq '[INSTALLDIR]HidHideClient.exe') 'Single enhanced UI shortcut'
 $actions=@{}; Rows CustomAction | ForEach-Object {$actions[$_.Values[0]]=$_.Values}
 $sequence=@{}; Rows InstallExecuteSequence | ForEach-Object {$sequence[$_.Values[0]]=$_.Values}
 foreach($name in @('InstallHidHideDriver','RemoveHidHideDriver')) {
  $type=[int]$actions[$name][1]; $undo=[int]$actions[($name+'_Rollback')][1]
  Check (($type -band 0xC00) -eq 0xC00 -and ($type -band 0x40) -eq 0) 'Checked deferred non-impersonating action'
  Check (($undo -band 0xD00) -eq 0xD00 -and ($undo -band 0x40) -eq 0) 'Checked deferred rollback action'
  Check ([int]$sequence[($name+'_Rollback')][2] -lt [int]$sequence[$name][2]) 'Rollback scheduled before forward action'
 }
 Check ([int]$sequence.InstallHidHideDriver[2] -gt [int]$sequence.InstallFiles[2]) 'Driver install after payload installation'
 Check ([int]$sequence.RemoveHidHideDriver[2] -lt [int]$sequence.RemoveFiles[2]) 'Driver uninstall before payload removal'
 Check ($sequence.RemoveHidHideDriver[1] -eq 'REMOVE="ALL" AND NOT UPGRADINGPRODUCTCODE') 'Old MSI upgrade removal retains driver'
 Check ([int]$sequence.RemoveExistingProducts[2] -gt [int]$sequence.InstallInitialize[2] -and [int]$sequence.RemoveExistingProducts[2] -lt [int]$sequence.ProcessComponents[2]) 'Major upgrade removes old MSI inside rollback transaction before component processing'
 $upgrades=@(Rows Upgrade)
 Check (@($upgrades | Where-Object {$_.Values[0] -eq $properties.UpgradeCode -and $_.Values[6] -eq 'WIX_UPGRADE_DETECTED'}).Count -eq 1) 'Major upgrade detects older unified MSI'
 Check (@($upgrades | Where-Object {$_.Values[0] -eq $properties.UpgradeCode -and $_.Values[6] -eq 'WIX_DOWNGRADE_DETECTED' -and ([int]$_.Values[4] -band 2) -ne 0}).Count -eq 1) 'Newer unified MSI detection is non-removing'
 Check (@(Rows LaunchCondition | Where-Object {$_.Values[0] -eq 'HIDHIDE_TRANSACTION OR UPGRADINGPRODUCTCODE'}).Count -eq 1) 'Standalone MSI requires preparation'
 "$script:checks private MSI structure checks passed. This does not execute lifecycle actions."
} finally {
 [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($db)
 [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($wi)
}
