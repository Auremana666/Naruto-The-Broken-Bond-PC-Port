[CmdletBinding()]
param(
    [string]$SdkPrefix,
    [ValidateRange(1, 128)]
    [int]$Jobs = 6,
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $SdkPrefix) { $SdkPrefix = Join-Path $projectRoot 'build/sdk-install' }
$SdkPrefix = [IO.Path]::GetFullPath($SdkPrefix)
if (-not (Test-Path -LiteralPath (Join-Path $SdkPrefix 'lib/cmake/rexglue/rexglueConfig.cmake'))) {
    throw 'Build the SDK with scripts/build-sdk.ps1 or pass -SdkPrefix pointing to an installed SDK.'
}
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $visualStudio) { throw 'Visual Studio Build Tools with the C++ workload are required.' }
    & (Join-Path $visualStudio 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}

$buildRoot = Join-Path $projectRoot 'build/launcher'
& cmake -S (Join-Path $projectRoot 'launcher') -B $buildRoot -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_CXX_COMPILER=clang++' "-DCMAKE_PREFIX_PATH=$SdkPrefix" "-Drexglue_DIR=$SdkPrefix/lib/cmake/rexglue"
if ($LASTEXITCODE -ne 0) { throw 'Launcher configuration failed.' }
& cmake --build $buildRoot --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Launcher build failed.' }
if ($Test) {
    & cmake --build $buildRoot --target broken_bond_launcher_test --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Launcher test build failed.' }
    $testRoot = Join-Path $buildRoot ('test-data/' + [guid]::NewGuid().ToString('N'))
    & (Join-Path $buildRoot 'broken_bond_launcher_test.exe') $testRoot
    if ($LASTEXITCODE -ne 0) { throw 'Launcher regression tests failed.' }
}
$gameBuild = Join-Path $projectRoot 'build/game'
New-Item -ItemType Directory -Force $gameBuild | Out-Null
Copy-Item -LiteralPath (Join-Path $buildRoot 'broken_bond_launcher.exe') -Destination $gameBuild -Force
& cmake -E copy_directory_if_different (Join-Path $buildRoot 'assets') (Join-Path $gameBuild 'assets')
if ($LASTEXITCODE -ne 0) { throw 'Launcher font copy failed.' }
Write-Host "Launcher: $(Join-Path $gameBuild 'broken_bond_launcher.exe')"
