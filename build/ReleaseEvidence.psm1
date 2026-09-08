Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Read-SourceGit([string]$Repository, [string]$Arguments) {
    if ($Repository -match '["\r\n]' -or $Repository.EndsWith('\')) { throw 'Unsupported source repository path.' }
    $info = [Diagnostics.ProcessStartInfo]::new((Get-Command git -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source)
    $info.Arguments = '-C "' + $Repository + '" ' + $Arguments
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $process = [Diagnostics.Process]::Start($info)
    try {
        $output = $process.StandardOutput.ReadToEndAsync()
        $errors = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(30000)) { throw 'Source inventory Git command timed out.' }
        $result = $output.GetAwaiter().GetResult()
        $diagnostic = $errors.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { throw "Source inventory Git command failed: $diagnostic" }
        if ($diagnostic) { Write-Verbose $diagnostic }
        return $result
    } finally { $process.Dispose() }
}

function Get-SourceEvidence([string]$Repository) {
    $Repository = (Resolve-Path -LiteralPath $Repository).Path
    $commit = (Read-SourceGit $Repository 'rev-parse HEAD').Trim()
    # NUL separation preserves spaces and non-ASCII names without Git's quoted
    # filename syntax. A missing listed source is an error, not silently omitted.
    $status = @((Read-SourceGit $Repository 'status --porcelain=v1 --untracked-files=all -z').Split([char]0) | Where-Object { $_.Length -ne 0 })
    $paths = @((Read-SourceGit $Repository 'ls-files --cached --others --exclude-standard -z').Split([char]0) | Where-Object { $_.Length -ne 0 } | Sort-Object -Unique)
    $files = @($paths | ForEach-Object {
        $path = Join-Path $Repository $_
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            [ordered]@{ path=$_; sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
        } elseif (Test-Path -LiteralPath $path -PathType Container) {
            throw "Nested source repository must be inventoried explicitly: $_"
        } else {
            # A tracked deletion is source evidence as well.
            [ordered]@{ path=$_; deleted=$true }
        }
    })
    $canonical = [ordered]@{ commit=$commit; status=$status; files=$files } | ConvertTo-Json -Depth 6 -Compress
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { $digest = [BitConverter]::ToString($algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($canonical))).Replace('-', '') }
    finally { $algorithm.Dispose() }
    [pscustomobject]@{ Commit=$commit; Dirty=($status.Count -ne 0); Status=$status; Files=$files; Digest=$digest }
}

Export-ModuleMember -Function Get-SourceEvidence
