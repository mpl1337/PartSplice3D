[CmdletBinding()]
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot 'build\Release'),
    [string]$OutputDir = (Join-Path $PSScriptRoot 'dist\PartSplice3D-v1.0.0-Windows-x64'),
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$releaseDir = [IO.Path]::GetFullPath($BuildDir)
$destination = [IO.Path]::GetFullPath($OutputDir)
$archive = $destination.TrimEnd([IO.Path]::DirectorySeparatorChar) + '.zip'
$distRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'dist'))

if (-not (Test-Path -LiteralPath $releaseDir -PathType Container)) {
    throw "Release-Ordner nicht gefunden: $releaseDir"
}
if (-not $destination.StartsWith($distRoot + [IO.Path]::DirectorySeparatorChar,
                                 [StringComparison]::OrdinalIgnoreCase)) {
    throw "Der Paketordner muss innerhalb von '$distRoot' liegen."
}
if (Test-Path -LiteralPath $destination) {
    if (-not $Force) { throw "Paketordner existiert bereits. Mit -Force bewusst neu erzeugen: $destination" }
    Remove-Item -LiteralPath $destination -Recurse -Force
}
if (Test-Path -LiteralPath $archive) {
    if (-not $Force) { throw "Paketarchiv existiert bereits. Mit -Force bewusst neu erzeugen: $archive" }
    Remove-Item -LiteralPath $archive -Force
}

New-Item -ItemType Directory -Path $destination | Out-Null

$requiredFiles = @(
    'PartSplice3D.exe', 'PartSpliceWorker.exe',
    'TKDESTEP.dll', 'TKXSBase.dll', 'TKMesh.dll', 'TKShHealing.dll',
    'TKTopAlgo.dll', 'TKBRep.dll', 'TKMath.dll', 'TKernel.dll'
)
foreach ($name in $requiredFiles) {
    $source = Join-Path $releaseDir $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Erforderliche Release-Datei fehlt: $source"
    }
}

Copy-Item -LiteralPath (Join-Path $releaseDir 'PartSplice3D.exe') -Destination $destination
Copy-Item -LiteralPath (Join-Path $releaseDir 'PartSpliceWorker.exe') -Destination $destination
Get-ChildItem -LiteralPath $releaseDir -Filter '*.dll' -File |
    Copy-Item -Destination $destination

foreach ($name in @('README.md', 'LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md',
                    'OPEN_CASCADE_SOURCE_OFFER.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $destination
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party_licenses') `
    -Destination (Join-Path $destination 'third_party_licenses') -Recurse

$hashLines = Get-ChildItem -LiteralPath $destination -File -Recurse |
    Where-Object Name -ne 'SHA256SUMS.txt' |
    Sort-Object FullName |
    ForEach-Object {
        $relative = [IO.Path]::GetRelativePath($destination, $_.FullName).Replace('\', '/')
        '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash, $relative
    }
$hashLines | Set-Content -LiteralPath (Join-Path $destination 'SHA256SUMS.txt') -Encoding ascii

Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $archive `
    -CompressionLevel Optimal

$manifest = Get-ChildItem -LiteralPath $destination -File -Recurse
"Paket erstellt: $destination"
"ZIP erstellt: $archive"
"Dateien: $($manifest.Count)"
"Größe: $([math]::Round(($manifest | Measure-Object Length -Sum).Sum / 1MB, 2)) MiB"
