[CmdletBinding()]
param(
    [string]$IsoPath,
    [string]$Destination
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $IsoPath) {
    $images = @(Get-ChildItem -LiteralPath $projectRoot -Filter '*.iso' -File)
    if ($images.Count -ne 1) { throw 'Specify -IsoPath when the project root does not contain exactly one ISO.' }
    $IsoPath = $images[0].FullName
}
$IsoPath = (Resolve-Path -LiteralPath $IsoPath).Path
if (-not $Destination) { $Destination = Join-Path $projectRoot 'game' }
$Destination = [IO.Path]::GetFullPath($Destination)
if ((Test-Path -LiteralPath $Destination) -and @(Get-ChildItem -LiteralPath $Destination -Force).Count) {
    throw "Extraction destination is not empty: $Destination"
}

$extractor = & (Join-Path $PSScriptRoot 'get-extract-xiso.ps1')
& $extractor -x -d $Destination $IsoPath
if ($LASTEXITCODE -ne 0) { throw 'extract-xiso failed. Preserve the output for inspection before retrying.' }
if (-not (Test-Path -LiteralPath (Join-Path $Destination 'default.xex'))) {
    throw 'Extraction completed without the expected default.xex.'
}
Get-FileHash -LiteralPath (Join-Path $Destination 'default.xex') -Algorithm SHA256
