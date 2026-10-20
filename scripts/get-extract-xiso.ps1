[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolRoot = Join-Path $projectRoot 'build/tools'
$archive = Join-Path $toolRoot 'extract-xiso.zip'
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
Join-Path $toolRoot 'extract-xiso/artifacts/extract-xiso.exe'
