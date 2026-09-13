Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Read-DriverManifest([string]$Path) {
    $m = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if ($m.schemaVersion -ne 1 -or $m.architecture -cne 'x64') { throw 'Unsupported driver manifest schema or architecture.' }
    if ($m.url -cne "https://github.com/nefarius/HidHide/releases/download/$($m.release)/$($m.archiveName)" -or
        $m.archiveName -cnotmatch '^HidHide_[0-9.]+_x64\.exe$' -or $m.release -cnotmatch '^v[0-9.]+$') {
        throw 'Invalid driver archive origin.'
    }
    foreach ($hash in @($m.archiveSha256, $m.archiveSignerThumbprint, $m.catalogSignerThumbprint)) {
        if ($hash -cnotmatch '^([A-F0-9]{64}|[A-F0-9]{40})$') { throw 'Invalid manifest digest or certificate identity.' }
    }
    if ($m.archiveSha256.Length -ne 64 -or $m.archiveSignerThumbprint.Length -ne 40 -or $m.catalogSignerThumbprint.Length -ne 40) { throw 'Invalid digest length.' }
    if ($m.driverVersion -cnotmatch '^\d+\.\d+\.\d+\.\d+$' -or $m.infDriverVer -cnotmatch ('^\d{2}/\d{2}/\d{4},' + [regex]::Escape($m.driverVersion) + '$')) { throw 'Invalid driver version.' }
    $required = @('HidHide.inf','HidHide.sys','hidhide.cat','LICENSE.rtf')
    if (@($m.files).Count -ne 4) { throw 'Driver manifest must contain exactly the four reviewed payload files.' }
    foreach ($name in $required) {
        $f = @($m.files | Where-Object { $_.name -ceq $name })
        if ($f.Count -ne 1 -or $f[0].sha256 -cnotmatch '^[A-F0-9]{64}$' -or
            $f[0].source -cnotmatch ('^[A-Za-z0-9]+/x64/HidHide/' + [regex]::Escape($name) + '$')) { throw "Invalid or duplicate manifest file: $name" }
        if ($f[0].catalogMember -isnot [bool] -or $f[0].catalogMember -ne ($name -in @('HidHide.inf','HidHide.sys'))) { throw "Invalid catalog coverage: $name" }
    }
    return $m
}

function Assert-DriverHash([string]$Path, [string]$Expected) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing signed payload file: $Path" }
    if ((Get-Item -LiteralPath $Path).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Payload cannot be a reparse point: $Path" }
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -cne $Expected) { throw "SHA256 mismatch: $Path" }
}

function Invoke-DriverVerification([string]$SignTool, [string[]]$Arguments) {
    $output = & $SignTool @Arguments 2>&1
    $code = $LASTEXITCODE
    $output | Write-Verbose
    if ($code -ne 0) { throw "SignTool verification failed ($code): $($Arguments -join ' ')`n$($output -join "`n")" }
}

function Assert-SignedDriver([object]$Manifest, [string]$Directory, [string]$SignTool) {
    foreach ($f in $Manifest.files) { Assert-DriverHash (Join-Path $Directory $f.name) $f.sha256 }
    $cat = Join-Path $Directory 'hidhide.cat'
    Invoke-DriverVerification $SignTool @('verify','/kp','/v',$cat)
    $signature = Get-AuthenticodeSignature -LiteralPath $cat
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -cne $Manifest.catalogSignerThumbprint) { throw 'Unexpected or untrusted driver catalog signer.' }
    foreach ($f in $Manifest.files | Where-Object catalogMember) {
        Invoke-DriverVerification $SignTool @('verify','/kp','/v','/c',$cat,(Join-Path $Directory $f.name))
    }
    $inf = Get-Content -LiteralPath (Join-Path $Directory 'HidHide.inf') -Raw
    if ($inf -notmatch ('(?m)^DriverVer\s*=\s*' + [regex]::Escape($Manifest.infDriverVer) + '\s*$') -or
        $inf -notmatch '(?m)^\[Standard\.NTamd64\]\s*$' -or $inf -notmatch 'HidHide_Device,root\\HidHide') { throw 'Unexpected INF version, architecture or device identity.' }
    $sys = Join-Path $Directory 'HidHide.sys'
    $bytes = [IO.File]::ReadAllBytes($sys)
    if ($bytes.Length -lt 64 -or [BitConverter]::ToUInt16($bytes,0) -ne 0x5A4D) { throw 'Invalid driver PE header.' }
    $pe = [BitConverter]::ToInt32($bytes,60)
    if ($pe -lt 64 -or $pe -gt $bytes.Length - 6 -or [BitConverter]::ToUInt32($bytes,$pe) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes,$pe+4) -ne 0x8664) { throw 'Driver is not an AMD64 PE.' }
    if ([Diagnostics.FileVersionInfo]::GetVersionInfo($sys).FileVersion -cne $Manifest.driverVersion) { throw 'Driver binary version mismatch.' }
}

Export-ModuleMember -Function Read-DriverManifest,Assert-DriverHash,Invoke-DriverVerification,Assert-SignedDriver
