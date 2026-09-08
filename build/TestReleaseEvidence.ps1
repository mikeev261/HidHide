[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Import-Module "$PSScriptRoot\ReleaseEvidence.psm1" -Force
$scratch = Join-Path $PSScriptRoot ('..\artifacts\source-evidence-test-' + [guid]::NewGuid())
$scratch = (New-Item -ItemType Directory -Path $scratch).FullName
function Invoke-FixtureGit([string[]]$Arguments) {
    & git -C $scratch @Arguments
    if ($LASTEXITCODE -ne 0) { throw 'Isolated source-evidence fixture failed.' }
}
$script:checks = 0
function Check([bool]$condition, [string]$name) { if (!$condition) { throw $name }; $script:checks++ }
Invoke-FixtureGit @('init','--quiet')
[IO.File]::WriteAllText((Join-Path $scratch 'source ü space.txt'), 'initial')
[IO.File]::WriteAllText((Join-Path $scratch '.gitignore'), "ignored/`n")
Invoke-FixtureGit @('add','--all')
Invoke-FixtureGit @('-c','commit.gpgSign=false','-c',('core.hooksPath=' + (Join-Path $scratch '.git\no-hooks')),'-c','user.name=Evidence Test','-c','user.email=evidence-test@example.invalid','commit','--quiet','-m','Isolated test fixture')
$clean = Get-SourceEvidence $scratch
Check (!$clean.Dirty -and $clean.Files.Count -eq 2) 'Committed fixture should be clean'
Check (@($clean.Files | Where-Object { $_.path -ceq 'source ü space.txt' }).Count -eq 1) 'NUL Git inventory preserves Unicode and spaces'
Check ((Get-SourceEvidence $scratch).Digest -ceq $clean.Digest) 'Unchanged source digest must be stable'
New-Item -ItemType Directory -Path (Join-Path $scratch 'ignored') | Out-Null
[IO.File]::WriteAllText((Join-Path $scratch 'ignored\output'), 'build output')
Check ((Get-SourceEvidence $scratch).Digest -ceq $clean.Digest) 'Ignored build output must not change source evidence'
[IO.File]::WriteAllText((Join-Path $scratch 'source ü space.txt'), 'changed')
$edited = Get-SourceEvidence $scratch
Check ($edited.Dirty -and $edited.Digest -cne $clean.Digest) 'Tracked source edit must invalidate build snapshot'
[IO.File]::WriteAllText((Join-Path $scratch 'new source.txt'), 'new')
$added = Get-SourceEvidence $scratch
Check ($added.Files.Count -eq 3 -and $added.Digest -cne $edited.Digest) 'New source must be captured before commit'
Remove-Item -LiteralPath (Join-Path $scratch 'source ü space.txt')
$deleted = Get-SourceEvidence $scratch
Check (@($deleted.Files | Where-Object { $_.path -ceq 'source ü space.txt' -and $_.deleted }).Count -eq 1) 'Tracked deletion must remain explicit in source evidence'
"$script:checks source-evidence checks passed. Isolated fixture: $scratch"
