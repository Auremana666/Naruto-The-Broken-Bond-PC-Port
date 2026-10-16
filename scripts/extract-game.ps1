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

$toolRoot = Join-Path $projectRoot 'build/tools'
$archive = Join-Path $toolRoot 'extract-xiso.zip'
$extractor = Join-Path $toolRoot 'extract-xiso/artifacts/extract-xiso.exe'
$release = 'build-202609111233'
$archiveHash = 'FC891DFA72F08E3838A2F8B7EC34A8E4D61678DA1CC8FEE879677F00BF114CE3'
New-Item -ItemType Directory -Force $toolRoot | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest "https://github.com/XboxDev/extract-xiso/releases/download/$release/extract-xiso-Win64_Release.zip" -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $archiveHash) {
    throw 'The extract-xiso archive does not match the pinned SHA-256.'
}
Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $toolRoot 'extract-xiso') -Force
& $extractor -x -d $Destination $IsoPath
if ($LASTEXITCODE -ne 0) { throw 'extract-xiso failed. Preserve the output for inspection before retrying.' }
if (-not (Test-Path -LiteralPath (Join-Path $Destination 'default.xex'))) {
    throw 'Extraction completed without the expected default.xex.'
}
Get-FileHash -LiteralPath (Join-Path $Destination 'default.xex') -Algorithm SHA256
