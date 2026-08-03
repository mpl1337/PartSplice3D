[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string] $File,

    [string] $Thumbprint = $env:PARTSPLICE_SIGN_CERT_SHA1,

    [ValidatePattern('^https://')]
    [string] $TimestampUrl = 'https://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
$normalizedThumbprint = ($Thumbprint -replace '\s', '').ToUpperInvariant()
if ($normalizedThumbprint -notmatch '^[0-9A-F]{40}$') {
    throw 'PARTSPLICE_SIGN_CERT_SHA1 muss der 40-stellige SHA-1-Fingerabdruck eines Codesignaturzertifikats sein.'
}

$certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$normalizedThumbprint" -ErrorAction SilentlyContinue
if (-not $certificate) {
    throw "Codesignaturzertifikat $normalizedThumbprint wurde unter Cert:\CurrentUser\My nicht gefunden."
}
if (-not $certificate.HasPrivateKey) { throw 'Das Codesignaturzertifikat besitzt keinen privaten Schlüssel.' }
if ($certificate.NotAfter -le (Get-Date)) { throw 'Das Codesignaturzertifikat ist abgelaufen.' }

$signTool = Get-Command signtool.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source
if (-not $signTool) {
    $kitsBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $signTool = Get-ChildItem -LiteralPath $kitsBin -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
}
if (-not $signTool) { throw 'signtool.exe wurde nicht gefunden. Installiere das Windows 10/11 SDK.' }

& $signTool sign /fd SHA256 /sha1 $normalizedThumbprint /tr $TimestampUrl /td SHA256 /v $File
if ($LASTEXITCODE -ne 0) { throw "SignTool ist mit Exitcode $LASTEXITCODE fehlgeschlagen." }
& $signTool verify /pa /all /v $File
if ($LASTEXITCODE -ne 0) { throw "Signaturprüfung ist mit Exitcode $LASTEXITCODE fehlgeschlagen." }
Write-Host "Signatur erfolgreich geprüft: $File"
