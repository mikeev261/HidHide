[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Path, [Parameter(Mandatory=$true)][string]$Out)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$wi = New-Object -ComObject WindowsInstaller.Installer
$db = $wi.OpenDatabase((Resolve-Path -LiteralPath $Path).Path, 0)
try {
    $tables = @('Property','CustomAction','InstallExecuteSequence','ServiceInstall','ServiceControl','Registry','Upgrade','File','Media')
    $rows = foreach ($table in $tables) {
        $view = $db.OpenView(('SELECT * FROM `' + $table + '`'))
        try {
            $view.Execute()
            while ($record = $view.Fetch()) {
                try {
                    # Explicit COM property access also works in PowerShell 7.
                    $count = $record.GetType().InvokeMember('FieldCount','GetProperty',$null,$record,$null)
                    $values = @(for ($i=1; $i -le $count; $i++) { $record.StringData($i) })
                    [pscustomobject]@{ Table=$table; Values=$values }
                } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($record) }
            }
        } finally { $view.Close(); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view) }
    }
    [pscustomobject]@{ SchemaVersion=1; Sha256=(Get-FileHash -LiteralPath $Path).Hash; Tables=@($rows) } |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $Out
} finally {
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($db)
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($wi)
}
