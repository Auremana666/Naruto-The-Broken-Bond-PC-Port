[CmdletBinding()]
param([ValidateRange(1, 128)][int]$Jobs = 6)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $visualStudio) { throw 'Visual Studio C++ Build Tools are required.' }
    & (Join-Path $visualStudio 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}
$buildRoot = Join-Path $projectRoot 'build/setup-helper'
$extractor = & (Join-Path $PSScriptRoot 'get-extract-xiso.ps1')
& cmake -S (Join-Path $projectRoot 'packaging') -B $buildRoot -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_CXX_COMPILER=clang++'
if ($LASTEXITCODE -ne 0) { throw 'Setup helper configuration failed.' }
& cmake --build $buildRoot --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Setup helper build failed.' }
Copy-Item -LiteralPath $extractor -Destination $buildRoot -Force
Write-Host "Setup helper: $buildRoot/broken_bond_setup_helper.exe"
