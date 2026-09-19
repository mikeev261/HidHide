[CmdletBinding()]
param([Parameter(Mandatory)][string]$Destination)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath("$PSScriptRoot\..")
$editor=Join-Path $repo 'Editor'
$version=([xml](Get-Content "$repo\ProductVersion.props" -Raw)).Project.PropertyGroup.HidHideProductVersion
if ((Get-Content "$editor\package.json" -Raw | ConvertFrom-Json).version -ne ([version]$version).ToString(3)) { throw 'Editor package version differs from ProductVersion.props.' }
$env:npm_config_cache=Join-Path $repo 'artifacts\npm-cache'
$env:electron_config_cache=Join-Path $repo 'artifacts\electron-cache'
$npm=(Get-Command npm.cmd -ErrorAction Stop).Source
Push-Location $editor
try {
 & $npm ci --no-audit --no-fund
 if ($LASTEXITCODE) { throw 'Editor dependency restore failed.' }
 foreach($task in @('build','test','package')) {
  & $npm run $task
  if ($LASTEXITCODE) { throw "Editor $task failed." }
 }
 $source=Join-Path $editor 'out\HidHideProfiles-win32-x64'
 if (!(Test-Path -LiteralPath "$source\HidHideProfiles.exe")) { throw 'Editor executable was not packaged.' }
 if (Test-Path -LiteralPath $Destination) { throw 'Editor staging destination must be new.' }
 Copy-Item -LiteralPath $source -Destination $Destination -Recurse
} finally { Pop-Location }
